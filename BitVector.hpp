/**
 * \file
 * \brief Implements the BitVector class, which computes basic arithmetic on
 * arbitrary-length arrays of bits.
 * 
 * This file makes significant use of preprocessor macros to (hopefully) reduce
 * the complexity of implementing operations on the bitvectors. Be sure to build
 * with a sufficient optimization level to simplify many of the computations.
 *
 * \license
 * Copyright (c) 2013 Ryan Govostes
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>


/**
 * \typedef word_t
 * \brief A numeric data type that can preferably be stored in a single register
 */
typedef unsigned long word_t;

/**
 * \def CEILDIV(a, d)
 * \brief Performs a division a / d but rounds up non-integer results
 */
#define CEILDIV(a, d) (((a) + (d) - 1) / (d))

/**
 * \def BITS_PER_BYTE
 * \brief The number of bits per byte
 */
#define BITS_PER_BYTE 8

/**
 * \def BYTES_PER_WORD
 * \brief The number of bytes per word
 */
#define BYTES_PER_WORD sizeof(word_t)

/**
 * \def BITS_PER_WORD
 * \brief The number of bits per word
 */
#define BITS_PER_WORD (BITS_PER_BYTE * BYTES_PER_WORD)

/**
 * \def BYTES_TO_BITS(n)
 * \brief Converts n bytes to the corresponding number of bits
 */
#define BYTES_TO_BITS(n) (BITS_PER_BYTE * (n))

/**
 * \def BITS_TO_BYTES(n)
 * \brief Converts n bits to the corresponding number of bytes
 */
#define BITS_TO_BYTES(n) CEILDIV((n), BITS_PER_BYTE)

/**
 * \def BYTES_TO_WORDS(n)
 * \brief Converts n bytes to the corresponding number of words
 */
#define BYTES_TO_WORDS(n) CEILDIV((n), BYTES_PER_WORD)

/**
 * \def WORDS_TO_BYTES(n)
 * \brief Converts n words to the correponding number of bytes
 */
#define WORDS_TO_BYTES(n) (BYTES_PER_WORD * (n))

/**
 * \def BITS_TO_WORDS(n)
 * \brief Converts n bits to the corresonding number of words
 */
#define BITS_TO_WORDS(n) BYTES_TO_WORDS(BITS_TO_BYTES(n))

/**
 * \def WORDS_TO_BITS(n)
 * \brief Converts n words to the corresponding number of bits
 */
#define WORDS_TO_BITS(n) BYTES_TO_BITS(WORDS_TO_BYTES(n))

/**
 * \def MASK_WITH_LOWER_BITS(n)
 * \brief Creates a bitmask with only the n lowest-order bits set
 */
#define MASK_WITH_LOWER_BITS(n) ((((word_t)1) << (n)) - 1)

/**
 * \def MAKS_WITH_BIT(n)
 * \brief Creates a bitmask with only the bit in position n set
 */
#define MASK_WITH_BIT(n) (((word_t)1) << (n))

/**
 * \def WORD_INDEX_FOR_BIT_IN_ARRAY(n)
 * \brief Returns the index of the word in the array that contains this bit
 *
 * Note that this index may exceed the in-place storage and may need to be
 * converted to an offset within the additional heap storage.
 *
 * For example, the 1 bit is in the 7th word, in position 3:
 *
 * Word# 0        1        2        3        4        5        6        7 
 *       -----------------------------------------------------------------------
 *       00000000 00000000 00000000 00000000 00000000 00000000 00000000 00010000
 *       -----------------------------------------------------------------------
 *  Bit# 01234567 01234567 01234567 01234567 01234567 01234567 01234567 01234567
 */
#define WORD_INDEX_FOR_BIT_IN_ARRAY(n) ((n) / BITS_PER_WORD)

/**
 * \def BIT_POSITION_FOR_BIT_IN_WORD(n)
 * \brief Returns the index of the bit within the word that contains the bit
 *
 * See example for WORD_INDEX_FOR_BIT_IN_ARRAY()
 */
#define BIT_POSITION_FOR_BIT_IN_WORD(n) ((n) % BITS_PER_WORD)

/**
 * \def OFFSET_OF_WORD_IN_HEAP(n)
 * \brief Returns the offset of the word from the start of heap storage, or a
 * negative number if the word is not on the heap.
 */
#define OFFSET_OF_WORD_IN_HEAP(n) (ssize_t)((n) - BITS_TO_WORDS(N))

/**
 * \def WORD_FROM(v, n)
 * \brief Can be used to refer to a word by its index without caring where that
 * word is stored.
 *
 * \param v - 
 */
#define WORD_FROM(v, n) \
  (*((OFFSET_OF_WORD_IN_HEAP(n) < 0) ? v.words + (n) \
                                     : v.morewords + OFFSET_OF_WORD_IN_HEAP(n)))


/**
 * \def HEAP_SIZE_IN_WORDS(n)
 * \brief The number of words allocated on the heap for an n-bit vector
 * 
 * This can and will be negative if there is no heap storage. Always check > 0.
 */
#define HEAP_SIZE_IN_WORDS(n) (BITS_TO_WORDS(n) - BITS_TO_WORDS(N))

/**
 * BitVector
 *
 * \brief An arbitrary-length array of bits that supports efficient arithmetic
 * operations.
 *
 * The array is created with some number of bits (N) in-place, which allows it
 * to avoid heap allocation when the actual number of elements is below that
 * threshold. This allows normal "small" cases to be fast without losing
 * generality for large inputs.
 *
 * Words are ordered least significant to most significant. Ordering of bits
 * within words is architecture dependent.
 */
template<size_t N>
class BitVector
{
public:
  /**
   * \brief Constructs a BitVector with the capacity for at least n bits
   *
   * If n is greater than N, the remaining required space is allocated on the
   * heap. Choose N wisely to avoid this allocation.
   *
   * If clear is set, memory allocated on the heap is cleared. Pass false if the
   * data on the heap is going to be overwritten immediately.
   */
  BitVector(size_t n, bool clear = true) : length(n), morewords(nullptr)
  {
    // Unset all bits; the default value of a BitVector is 0
    memset(words, 0, sizeof(words));
    
    // Allocate any additional storage needed on the heap
    size_t heapWordsNeeded = HEAP_SIZE_IN_WORDS(length);
    if (heapWordsNeeded > 0)
    {
      morewords = new word_t[heapWordsNeeded];
      if (clear)
        memset(morewords, 0, WORDS_TO_BYTES(heapWordsNeeded));
    }
  }
  
  ~BitVector()
  {
    delete [] morewords;
  }
  
  size_t width() const
  {
    return length;
  }
  
  std::string toBinaryString() const
  {
    char s[length + 1];
    for (size_t i = 0; i < length; i ++)
      s[i] = getBit(i) ? '1' : '0';
    s[length] = '\0';
    return std::string(s);
  }
  
  void setBit(size_t index, bool value)
  {
    size_t wordidx = WORD_INDEX_FOR_BIT_IN_ARRAY(index);
    size_t position = BIT_POSITION_FOR_BIT_IN_WORD(index);
    WORD(wordidx) |= MASK_WITH_BIT(position);
  }
  
  bool getBit(size_t index) const
  {
    size_t wordidx = WORD_INDEX_FOR_BIT_IN_ARRAY(index);
    size_t position = BIT_POSITION_FOR_BIT_IN_WORD(index);
    return (WORD(wordidx) & MASK_WITH_BIT(position)) != 0;
  }
  
  /**
   * \brief Pre-increment operator
   *
   * If incrementing a lower-order word causes an overflow to 0, then we need
   * to increment the next word as well to carry.
   */
  BitVector& operator++()
  {
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
    {
      if ((++ WORD(i)) != 0)
        break;
    }
    
    return *this;
  }
  
 /**
  * \brief Post-increment operator
  *
  * Use pre-increment when possible, as this requires an allocation.
  */
 BitVector operator++(int)
 {
   BitVector result<N>(length, false);
   
   for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
   {
     if ((++ WORD(i)) != 0)
       break;
   }
   
   return result;
 }
  
  /**
   * \brief Pre-decrement operator
   *
   * If a lower-order word is zero and we decrement causing an overflow, we need
   * to borrow from the next word.
   */
  BitVector& operator--()
  {
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
    {
      if ((WORD(i) --) != 0)
        return;
    }
  }
  
  /**
   * \brief Constructs a BitVector as a copy of another.
   */
  BitVector(const BitVector &other) : length(0), morewords(nullptr)
  {
    copyFrom(other);
  }
  
 /**
  * \brief Assigns a value to a BitVector, overwriting previous contents
  */
 BitVector &operator=(const BitVector &other)
 {
   copyFrom(other);
   return *this;
 }
  
protected:
  /**
   * \brief Resizes to the desired width, but this is not recommended
   * 
   * This doesn't try to manage heap storage very carefully; you may end up
   * with some redundant allocation and copying if you resize a bitvector often.
   * The class was not designed with this in mind, so it is not recommended.
   */
  void resize(size_t width, bool clear = true)
  {
    // The number of words on the heap needed to represent bitvectors of this
    // size
    size_t heapWordsNeeded = HEAP_SIZE_IN_WORDS(width);
    
    // The number of words that are currently on the heap
    size_t heapWordsCurrent = HEAP_SIZE_IN_WORDS(length);
    
    // If the new width can fit entirely in-place, we can free the heap storage
    if (width <= N)
    {
      delete [] morewords;
      morewords = nullptr;
    }
    
    // Otherwise, we may need to expand the heap storage, which is expensive
    else if (width > N && heapWordsNeeded > heapWordsCurrent)
    {
      word_t *newwords = new word_t[heapWordsNeeded];
      memcpy(newwords, morewords, WORDS_TO_BYTES(heapWordsCurrent));
      
      if (clear)
      {
        size_t remaining = WORDS_TO_BYTES(heapWordsNeeded) - \
          WORDS_TO_BYTES(heapWordsCurrent);
        memset(newwords + WORDS_TO_BYTES(heapWordsCurrent), 0, remaining);
      }
      
      delete [] morewords;
      morewords = newwords;
    }
    
    // The case (width > N && needed == current) is ignored because no change to
    // the heap is needed.
    
    // The case (width > N && needed < current) is ignored because while there
    // is now unused storage on the heap, releasing it would require copying.
    
    length = width;
  }
  
  /**
   * \brief Copies the width and contents of another BitVector into this one,
   * overwriting previous contents and allocating memory if necessary
   */
  void copyFrom(const BitVector &other)
  {
    // Do nothing if this is copying from itself
    if (this == &other)
      return;
    
    // Resize to the new length
    resize(other.length, false);
    
    // Copy the in-place words
    memcpy(words, other.words, sizeof(words));
    
    // Allocate any additional storage needed on the heap, and copy to it
    size_t heapWordsNeeded = HEAP_SIZE_IN_WORDS(length);
    if (heapWordsNeeded > 0)
    {
      morewords = new word_t[heapWordsNeeded];
      memcpy(morewords, other.morewords, WORDS_TO_BYTES(heapWordsNeeded));
    }
  }
  
  size_t length;
  word_t words[BITS_TO_WORDS(N)];
  word_t *morewords;
};
