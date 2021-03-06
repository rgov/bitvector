/**
 * \file
 * \brief Implements the BitVector class, which computes basic arithmetic on
 * fixed-length arrays of bits.
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

#include <string>
#include <cassert>
#include <cstdint>
#include <cstring>


/**
 * \typedef word_t
 * \brief A numeric data type that can be stored in a single register
 */
typedef uint64_t word_t;

/**
 * \typedef halfword_t
 * \brief A numeric data type that is half the width of a word_t
 * 
 * This is generally used when we want to know the magnitude of overflow, but
 * requires twice as many steps
 */
typedef uint32_t halfword_t;

/**
 * \def CEILDIV(a, d)
 * \brief Performs a division a / d but rounds up non-integer results
 */
#define CEILDIV(a, d) (((a) + (d) - 1) / (d))

/**
 * \def BITS_PER_BYTE
 */
#define BITS_PER_BYTE 8

/**
 * \def BYTES_PER_WORD
 */
#define BYTES_PER_WORD sizeof(word_t)

/**
 * \def BITS_PER_WORD
 */
#define BITS_PER_WORD (BITS_PER_BYTE * BYTES_PER_WORD)

/**
 * \def BYTES_TO_BITS(n)
 * \param n - number of bytes
 * \returns number of bits
 */
#define BYTES_TO_BITS(n) (BITS_PER_BYTE * (n))

/**
 * \def BITS_TO_BYTES(n)
 * \brief Converts n bits to the corresponding number of bytes
 * \param n - number of bits
 * \returns number of bytes
 */
#define BITS_TO_BYTES(n) CEILDIV((n), BITS_PER_BYTE)

/**
 * \def BYTES_TO_WORDS(n)
 * \param n - number of bytes
 * \returns number of words
 */
#define BYTES_TO_WORDS(n) CEILDIV((n), BYTES_PER_WORD)

/**
 * \def WORDS_TO_BYTES(n)
 * \param n - number of words
 * \returns number of bytes
 */
#define WORDS_TO_BYTES(n) (BYTES_PER_WORD * (n))

/**
 * \def BITS_TO_WORDS(n)
 * \param n - number of bits
 * \returns number of bytes
 */
#define BITS_TO_WORDS(n) BYTES_TO_WORDS(BITS_TO_BYTES(n))

/**
 * \def WORDS_TO_BITS(n)
 * \param n - number of words
 * \returns number of bits
 */
#define WORDS_TO_BITS(n) BYTES_TO_BITS(WORDS_TO_BYTES(n))

/**
 * \def MASK_WITH_LOWER_BITS(n)
 * \brief Creates a bitmask with only the n lowest-order bits set
 * \param n - number of low-order bits set
 * \returns bitmask with n low-order bits set
 */
#define MASK_WITH_LOWER_BITS(n) ((((word_t)1) << (n)) - 1)

/**
 * \def MASK_WITH_BIT(n)
 * \brief Creates a bitmask with only the bit in position n set
 * \param n - bit index to be set
 * \returns bitmask with only the bit in position n set
 */
#define MASK_WITH_BIT(n) (((word_t)1) << (n))

/**
 * \def EXTRACT_BIT(w, n)
 * \brief Extracts the nth bit of a word
 * \param v - the word to extract from
 * \param n - the index of the bit to extract
 * \returns 1 if the bit is set, 0 otherwise
 */
#define EXTRACT_BIT(w, n) ((((word_t)(w)) >> (n)) & 1)

/**
 * \def WORD_INDEX_FOR_BIT_IN_ARRAY(n)
 * \brief Returns the index of the word in the array that contains this bit
 *
 * Note that this index may exceed the in-object storage and may need to be
 * converted to an offset within the additional heap storage.
 *
 * For example, the 1 bit is in the 7th word, in position 3:
 *
 * <PRE>
 * Word# 0        1        2        3        4        5        6        7 
 *       -----------------------------------------------------------------------
 *       00000000 00000000 00000000 00000000 00000000 00000000 00000000 00010000
 *       -----------------------------------------------------------------------
 *  Bit# 01234567 01234567 01234567 01234567 01234567 01234567 01234567 01234567
 * </PRE>
 *
 * \param n - the bit index
 * \returns index of the word that contains this bit
 */
#define WORD_INDEX_FOR_BIT_IN_ARRAY(n) ((n) / BITS_PER_WORD)

/**
 * \def BIT_POSITION_FOR_BIT_IN_WORD(n)
 * \brief Returns the index of the bit within the word that contains the bit
 *
 * See example for WORD_INDEX_FOR_BIT_IN_ARRAY()
 *
 * \param n - the bit index relative to the BitVector
 * \returns bit index relative to the word
 */
#define BIT_POSITION_FOR_BIT_IN_WORD(n) ((n) % BITS_PER_WORD)

/**
 * \def OFFSET_OF_WORD_IN_HEAP(n)
 * \brief Computes the offset of a word relative to heap storage
 * \param n - the word index
 * \returns the offset of the word from the start of heap storage, or a
 * negative number if the word is not on the heap
 */
#define OFFSET_OF_WORD_IN_HEAP(n) (ssize_t)((n) - BITS_TO_WORDS(N))

/**
 * \def WORD_FROM(v, n)
 * \brief Used to refer to a word by its index without caring where that word
 * is stored
 *
 * \param v - BitVector object containing this word
 * \param n - the word index
 * \returns when used as an R-value, the contents of the word at index n
 */
#define WORD_FROM(v, n) \
  (*((OFFSET_OF_WORD_IN_HEAP(n) < 0) \
    ? (v).words + (n) \
    : (v).morewords + OFFSET_OF_WORD_IN_HEAP(n)))

/**
 * \def WORD(n)
 * \brief Shortcut for WORD_FROM(*this, n)
 * \see WORD_FROM(n)
 */
#define WORD(n) WORD_FROM(*this, (n))

/**
 * \def HEAP_SIZE_IN_WORDS(n)
 * \brief Computes the number of words allocated on the heap
 *
 * Will be a bogus value if there is no heap storage, so check that the pointer
 * is non-NULL also.
 * 
 * \params n - the size of the vector in bits
 * \returns the size of the heap storage in words
 */
#define HEAP_SIZE_IN_WORDS(n) (BITS_TO_WORDS(n) - BITS_TO_WORDS(N))


/**
 * BitVector
 *
 * \brief A fixed-length array of bits that supports efficient arithmetic
 * operations.
 *
 * The array is created with some number of bits (N) in-object, which allows it
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
   * BitRef
   *
   * \brief Like std::bitset::reference, allows reading and setting a single
   * bit within the BitVector.
   *
   * These wrap BitVector::getBit, BitVector::setBit, and BitVector::flipBit.
   */
  class BitRef
  {
  public:
    BitRef &operator=(bool x)
    {
      bv.setBit(index, x);
    }
    
    BitRef &operator=(const BitRef &other)
    {
      bv.setBit(index, (bool)other);
    }
    
    void flip()
    {
      bv.flipBit(index);
    }
    
    operator bool() const
    {
      return ((const BitVector<N> &)bv).operator[](index);
    }
    
  private:
    friend class BitVector;
    
    BitRef() { }
    BitRef(BitVector<N> &BV, size_t Index) : bv(BV), index(Index) { }
    
    /**
     * \brief The BitVector this BitRef refers to
     */
    BitVector<N> &bv;
    
    /**
     * \brief The position of the referenced bit within the BitVector
     */
    size_t index;
  };
  
  
  /**
   * \brief Constructs a BitVector with the capacity for n bits
   *
   * If n is greater than N, the remaining required space is allocated on the
   * heap. Choose N wisely to avoid this allocation.
   *
   * \param n - the length of the BitVector
   * \param clear - if set, memory allocated on the heap is cleared. Pass false
   *   if the data on the heap is going to be overwritten immediately.
   */
  BitVector(size_t n, bool clear = true) : length(0), morewords(nullptr)
  {
    // Unset all bits; the default value of a BitVector is 0
    memset(words, 0, sizeof(words));
    
    // Resize to the specified size to allocate additional storage on the heap
    resize(n, clear);
  }
  
  BitVector(const BitVector &other) : length(0), morewords(nullptr)
  {
    copyFrom(other);
  }
  
  /**
   * \brief Constructs a BitVector from a string
   *
   * Only base 2 (binary) strings with no prefix are currently supported.
   *
   * \param string - a C string containing digits
   * \param radix - the base of the digits in the string (must be 2)
   */
  BitVector(const char *string, int radix) : length(0), morewords(nullptr)
  {
    assert(radix == 2 && "Only binary is supported");
    
    // Resize to one bit per character
    resize(strlen(string), false);
    if (length == 0)
      return;
    
    // Copy bits from the string
    const char *c = string + length - 1;
    for (size_t i = 0; i <= length; ++i, --c)
      setBit(i, *c == '1');
  }
  
  ~BitVector()
  {
    delete [] morewords;
  }
  
  size_t width() const
  {
    return length;
  }
  
  /**
   * \brief Generates a string representing the BitVector
   * 
   * Only base 2 (binary) strings are currently supported. Prefixes are not
   * prepended to the output.
   *
   * \param radix - the base to output in (must be 2)
   */
  std::string toString(int radix = 2) const
  {
    // The output is guaranteed to be at most this long
    char *s = new char[length + 1];
    
    if (radix == 2)
    {
      // Output bits from most to least significant
      for (size_t i = length, j = 0; i >= 2; --i, ++j)
        s[i] = getBit(j) ? '1' : '0';
      s[length] = '\0';
    }
    else
    {
      assert(false && "Not a supported radix");
    }
    
    // Return the string as a std::string
    std::string str(s);
    delete [] s;
    return str;
  }
  
  /**
   * \param index - the index of the bit
   * \returns true if the bit is 1, false otherwise
   */
  bool getBit(size_t index) const
  {
    size_t wordidx = WORD_INDEX_FOR_BIT_IN_ARRAY(index);
    size_t position = BIT_POSITION_FOR_BIT_IN_WORD(index);
    return (WORD(wordidx) & MASK_WITH_BIT(position)) != 0;
  }
  
  /**
   * \param index - the index of the bit
   * \param x - true if the bit should be set to 1, false otherwise
   */
  void setBit(size_t index, bool x)
  {
    size_t wordidx = WORD_INDEX_FOR_BIT_IN_ARRAY(index);
    size_t position = BIT_POSITION_FOR_BIT_IN_WORD(index);
    if (x)
      WORD(wordidx) |= MASK_WITH_BIT(position);
    else
      WORD(wordidx) &= ~(MASK_WITH_BIT(position));
  }
  
  /**
   * \param index - the index of the bit
   */
  void flipBit(size_t index)
  {
    size_t wordidx = WORD_INDEX_FOR_BIT_IN_ARRAY(index);
    size_t position = BIT_POSITION_FOR_BIT_IN_WORD(index);
    WORD(wordidx) ^= MASK_WITH_BIT(position);
  }
  
  /**
   * \returns the truth value of the specified bit
   */
  bool operator[](size_t index) const
  {
    return getBit(index);
  }
  
  /**
   * \returns a BitRef for the specified bit, supporting assignment
   */
  BitRef operator[](size_t index)
  {
    return BitRef(*this, index);
  }
  
  BitVector &operator|=(const BitVector &rhs)
  {
    assert(length == rhs.length && "Operands must have equal widths");
    
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
      WORD(i) |= WORD_FROM(rhs, i);
  }
  
  BitVector operator|(const BitVector &rhs) const
  {
    BitVector result(*this);
    result.operator|=(rhs);
    return result;
  }
  
  BitVector &operator&=(const BitVector &rhs)
  {
    assert(length == rhs.length && "Operands must have equal widths");
    
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
      WORD(i) &= WORD_FROM(rhs, i);
  }
  
  BitVector operator&(const BitVector &rhs) const
  {
    BitVector result(*this);
    result.operator&=(rhs);
    return result;
  }
  
  BitVector &operator^=(const BitVector &rhs)
  {
    assert(length == rhs.length && "Operands must have equal widths");
    
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
      WORD(i) &= WORD_FROM(rhs, i);
  }
  
  BitVector operator^(const BitVector &rhs) const
  {
    BitVector result(*this);
    result.operator^=(rhs);
    return result;
  }
  
  /**
   * \brief Logical left shift
   *
   * The endianness of storage confuses things a little bit here. Left shift
   * slides bits towards the more significant end.
   */
  BitVector &operator<<=(size_t count)
  {
    if (count == 0)
      return;
    
    // Easy case: count is a multiple of 8, so we can just slide bytes around
    if (count % BITS_PER_BYTE == 0)
    {
      slideBytesRight(count / 8); // Yes, _right_.
    }
    else
    {
      assert(false && "Shift amount is not supported yet");
    }
    
    return *this;
  }
  
  BitVector operator<<(size_t count)
  {
    BitVector result(*this);
    result <<= count;
    return result;
  }
  
  BitVector &operator++()
  {
    // If incrementing a lower-order word causes an overflow to 0, then we need
    // to increment the next word as well to carry.
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
    {
      if ((++ WORD(i)) != 0)
        break;
    }
    
    return *this;
  }
  
  BitVector operator++(int)
  {
    BitVector<N> result(*this);
    this->operator++();
    return result;
  }
  
  BitVector &operator--()
  {
    // If a lower-order word is zero and we decrement causing an underflow, we
    // need to borrow from the next word.
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
    {
      if ((WORD(i) --) != 0)
        return;
    }
  }
  
  BitVector operator--(int)
  {
    BitVector<N> result(*this);
    this->operator--();
    return result;
  }
  
  BitVector &operator+=(const BitVector &rhs)
  { 
    assert(length == rhs.length && "Operands must have equal widths");
    
    // Implementation of the full adder algorithm from the Wikipedia article
    // "Adder (electronics)"
    word_t carry = 0;
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
    {
      word_t x = WORD(i);
      word_t y = WORD_FROM(rhs, i);
      WORD(i) = x + y + carry;
      
      word_t highx = EXTRACT_BIT(x, BITS_PER_WORD - 1);
      word_t highy = EXTRACT_BIT(y, BITS_PER_WORD - 1);
      carry = (highx & highy) ^ (carry & (highx ^ highy));
    }
    
    return *this;
  }
  
  BitVector operator+(const BitVector &rhs) const
  {
    BitVector result(*this);
    result.operator+=(rhs);
    return result;
  }
  
  BitVector operator~() const
  {
    BitVector result(*this);
    result.complement();
    return result;
  }
  
  /**
   * \brief Computes the one's complement in-object
   * \returns a reference to the same BitVector
   */
  BitVector &complement()
  {
    for (size_t i = 0; i < BITS_TO_WORDS(length); i ++)
      WORD(i) ^= ~(word_t)0;
    return *this;
  }
  
  BitVector operator-() const
  {
    BitVector result(*this);
    result.negate();
    return result;
  }
  
  BitVector operator+() const
  {
    // Not sure why anyone would use this!
    BitVector result(*this);
    return result;
  }
  
  /**
   * \brief Computes the two's complement in-object
   * \returns a reference to the same BitVector
   */
  BitVector &negate()
  {
    return this->complement().operator++();
  }
  
  BitVector &operator=(const BitVector &other)
  {
    copyFrom(other);
    return *this;
  }
  
  bool operator==(const BitVector &rhs) const
  {
    assert(length == rhs.length && "Operands must have equal widths");
    
    // Compare all but the most significant word, which may be partial
    size_t lastidx = BITS_TO_WORDS(length) - 1;
    for (size_t i = 0; i < lastidx; ++i)
    {
      if (WORD(i) != WORD_FROM(rhs, i))
        return false;
    }
    
    // We'll mask out the unused portion of the most significant word
    word_t mask = MASK_WITH_LOWER_BITS(length % BITS_PER_WORD);
    return (WORD(lastidx) & mask) == (WORD_FROM(rhs, lastidx) & mask);
  }
  
  bool operator!=(const BitVector &rhs) const
  {
    return !(this->operator==(rhs));
  }
  
  bool operator<(const BitVector &rhs) const
  {
    assert(length == rhs.length && "Operands must have equal widths");
    
    // Start by comparing the most significant word
    size_t lastidx = BITS_TO_WORDS(length) - 1;
    word_t mask = MASK_WITH_LOWER_BITS(length % BITS_PER_WORD);
    if ((WORD(lastidx) & mask) < (WORD_FROM(rhs, lastidx) & mask))
      return true;
    
    // Now compare the rest, in reverse order. The indices are off by one
    // intentionally, sorry.
    for (size_t i = lastidx; i >= 1; --i)
    {
      if (WORD(i - 1) < WORD_FROM(rhs, i - 1))
        return true;
    }
    
    return false;
  }
  
  bool operator<=(const BitVector &rhs) const
  {
    return !(this->operator>(rhs));
  }
  
  bool operator>(const BitVector &rhs) const
  {
    return rhs.operator<(*this);
  }
  
  bool operator>=(const BitVector &rhs) const
  {
    return !(this->operator<(rhs));
  }
  
protected:
  /**
   * \brief Resizes the BitVector to the desired width
   *
   * This isn't recommended for performance reasons.
   * 
   * This doesn't try to manage heap storage very carefully; you may end up
   * with some redundant allocation and copying if you resize a BitVector often.
   * The class was not designed with this in mind, so it is not recommended.
   * 
   * \param width - the new width
   * \param clear - if set, zero out heap storage. Pass false
   *   if the data on the heap is going to be overwritten immediately.
   */
  void resize(size_t width, bool clear = true)
  {
    // The number of words on the heap needed to represent BitVectors of this
    // size
    size_t heapWordsNeeded = (width > N ? HEAP_SIZE_IN_WORDS(width) : 0);
    
    // The number of words that are currently on the heap
    size_t heapWordsCurrent =
      (morewords == nullptr ? 0 : HEAP_SIZE_IN_WORDS(length));
    
    // If the new width can fit entirely in-object, we can free the heap storage
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
   *
   * \param other - the BitVector to copy from
   */
  void copyFrom(const BitVector &other)
  {
    // Do nothing if this is copying from itself
    if (this == &other)
      return;
    
    // Resize to the new length
    resize(other.length, false);
    
    // Copy the in-object words
    memcpy(words, other.words, sizeof(words));
    
    // Allocate any additional storage needed on the heap, and copy to it
    size_t heapWordsNeeded = HEAP_SIZE_IN_WORDS(length);
    if (heapWordsNeeded > 0)
    {
      morewords = new word_t[heapWordsNeeded];
      memcpy(morewords, other.morewords, WORDS_TO_BYTES(heapWordsNeeded));
    }
  }
  
  /**
   * \brief Slides stored bytes to to the right (toward MSB)
   *
   * \param slide - the number of bytes to slide by
   * \param fill - the byte to fill in with on the left
   */
  void slideBytesRight(size_t slide, unsigned char fill = 0)
  {
    // Slide bytes on the heap first, filling in on the left from in-object
    // storage
    if (morewords)
    {
      memmove(morewords + slide, morewords, HEAP_SIZE_IN_WORDS(length) - slide);
      memcpy(morewords, words + BITS_TO_WORDS(N) - slide, slide);
    }
    
    // Slide bytes in in-object storage, filling in on the left
    memmove(words + slide, words, BITS_TO_WORDS(N) - slide);
    memset(words, fill, slide);
  }
  
  /**
   * \brief Slides stored bytes to to the left (toward LSB)
   *
   * \param slide - the number of bytes to slide by
   * \param fill - the byte to fill in with on the right
   */
  void slideBytesLeft(size_t slide, unsigned char fill = 0)
  {
    // Slide in-object bytes first
    memmove(words, words + slide, HEAP_SIZE_IN_WORDS(length) - slide);
    
    // Fill on the right or copy bytes from the heap
    if (!morewords)
    {
      memset(words + slide, fill, slide);
    }
    else
    {
      memcpy(words + slide, morewords, slide);
      
      // Now slide words on the heap and fill
      memmove(morewords, morewords + slide, HEAP_SIZE_IN_WORDS(length) - slide);
      memset(morewords + slide, fill, slide);
    }
  }
  
  /**
   * \brief The length of the BitVector in bits
   */
  size_t length;
  
  /**
   * \brief In-object storage of words
   */
  word_t words[BITS_TO_WORDS(N)];
  
  /**
   * \brief Additional heap storage if the length exceeds N words
   */
  word_t *morewords;
};
