//(Unused) This is a simple implementation of the QFS compression

#include "hash_chain.h"
#include <vector>

namespace qfs {

	using namespace std;

	typedef unsigned int uint;
	typedef vector<unsigned char> bytes;

	//copy length bytes from src at srcPos to dst at dstPos and increment srcPos and dstPos
	//copying one byte at a time is REQUIRED in some cases, otherwise decompression will not work
	void copyBytes(bytes& src, uint& srcPos, bytes& dst, uint& dstPos, uint length) {
		for(uint i = 0; i < length; i++) {
			dst[dstPos++] = src[srcPos++];
		}
	}

	//compresses src, returns an empty vector if compression fails
	//it will fail if the compressed output >= decompressed input since it's better to store these assets uncompressed
	//this typically happens with very small assets
	bytes compress(bytes& src) {
		//maximum accepted size of src is 0xFFFFFF due to the fact that the uncompressed size in the header is only 3 bytes long
		if(src.size() > 0xFFFFFF) {
			return bytes();
		}

		//compressed output has to smaller than the decompressed output, otherwise it's not useful
		bytes dst = bytes(src.size() - 1);
		Table table = Table(src);
		
		uint srcPos = 0;
		uint dstPos = 9;
		
		//plain = number of bytes to copy from src as is with no compression
		//nCopy = number of bytes to compress
		//offset = offset from current location in decompressed input to where the pattern could be found
		
		uint i = 0;
		while(i <= src.size() - 3) {
			Match match = table.getLongestMatch(i);
			
			if(match.length >= 3) {
				i = match.location + match.length;
			} else {
				i++;
				continue;
			}
			
			//copy bytes from src to dst until the location of the match is reached
			uint plain = match.location - srcPos;
			while(plain > 3) {
				//max possible value is 112
				//this can only be a multiple of 4, due to the 2-bit right shift in the control character
				plain = getMin(plain - plain % 4, 112);
				
				if(dstPos + plain + 1 > dst.size()) {
					return bytes();
				}
				
				// 111ppppp
				dst[dstPos++] = 0b11100000 + ((plain - 4) >> 2);
				
				copyBytes(src, srcPos, dst, dstPos, plain);
				
				plain = match.location - srcPos;
			}
			
			uint nCopy = match.length;
			uint offset = match.offset - 1; //subtraction is a part of the transformation for the offset, I think...
			
			//apply weird QFS transformations
			if(nCopy <= 10 && offset < 1024) {
				if(dstPos + plain + 2 > dst.size())  {
					return bytes();
				}
				
				// 0oocccpp oooooooo
				dst[dstPos++] = ((offset >> 3) & 0b01100000) + ((nCopy - 3) << 2) + plain;
				dst[dstPos++] = offset;
				
			} else if(nCopy <= 67 && offset < 16384) {
				if(dstPos + plain + 3 > dst.size())  {
					return bytes();
				}
				
				// 10cccccc ppoooooo oooooooo
				dst[dstPos++] = 0b10000000 + (nCopy - 4);
				dst[dstPos++] = (plain << 6) + (offset >> 8);
				dst[dstPos++] = offset;
				
			} else /*if(nCopy <= 1028 && offset < 131072)*/ {
				if(dstPos + plain + 4 > dst.size())  {
					return bytes();
				}
				
				// 110occpp oooooooo oooooooo cccccccc
				dst[dstPos++] = 0b11000000 + ((offset >> 12) & 0b00010000) + (((nCopy - 5) >> 6) & 0b00001100) + plain;
				dst[dstPos++] = offset >> 8;
				dst[dstPos++] = offset;
				dst[dstPos++] = nCopy - 5;
			}
			
			copyBytes(src, srcPos, dst, dstPos, plain);
			srcPos += nCopy;
		}
		
		//copy the remaining bytes at the end
		uint plain = src.size() - srcPos;
		while(plain > 3) {
			plain = getMin(plain - plain % 4, 112);
			
			if(dstPos + plain + 1 > dst.size())  {
				return bytes();
			}
			
			// 111ppppp
			dst[dstPos++] = 0b11100000 + ((plain - 4) >> 2);
			
			copyBytes(src, srcPos, dst, dstPos, plain);
			
			plain = src.size() - srcPos;
		}
		
		if(plain > 0) {
			if(dstPos + plain + 1 > dst.size())  {
				return bytes();
			}
			
			// 111111pp
			dst[dstPos++] = 0b11111100 + plain;
			
			copyBytes(src, srcPos, dst, dstPos, plain);
		}
		
		//make QFS compression header
		uint compressedSize = dstPos;
		uint uncompressedSize = src.size();
		
		dst[0] = compressedSize;
		dst[1] = compressedSize >> 8;
		dst[2] = compressedSize >> 16;
		dst[3] = compressedSize >> 24;
		dst[4] = 0x10;
		dst[5] = 0xFB;
		dst[6] = uncompressedSize >> 16;
		dst[7] = uncompressedSize >> 8;
		dst[8] = uncompressedSize;
		
		dst.resize(dstPos);
		return dst;
	}

	//decompresses src, returns an empty vector if decompression fails
	//it should only fail on broken compressed assets
	bytes decompress(bytes& src) {
		uint srcPos = 6;
		uint uncompressedSize = ((uint) src[srcPos++] << 16) + ((uint) src[srcPos++] << 8) + ((uint) src[srcPos++]);
		
		bytes dst = bytes(uncompressedSize);
		uint dstPos = 0;
		
		uint b0, b1, b2, b3; //control characters
		uint nCopy, offset, plain;
		
		while(srcPos < src.size()) {
			b0 = src[srcPos++];

			if(b0 < 0x80) {
				if(srcPos + 1 > src.size()) {
					return bytes();
				}
				
				b1 = src[srcPos++];
				
				// 0oocccpp oooooooo
				plain = b0 & 0b00000011; //0-3
				nCopy = ((b0 & 0b00011100) >> 2) + 3; //3-10
				offset = ((b0 & 0b01100000) << 3) + b1 + 1; //1-1024
				
			} else if(b0 < 0xC0) {
				if(srcPos + 2 > src.size()) {
					return bytes();
				}
				
				b1 = src[srcPos++];
				b2 = src[srcPos++];
				
				// 10cccccc ppoooooo oooooooo
				plain = (b1 & 0b11000000) >> 6; //0-3
				nCopy = (b0 & 0b00111111) + 4; //4-67
				offset = ((b1 & 0b00111111) << 8) + b2 + 1; //1-16384
				
			} else if(b0 < 0xE0) {
				if(srcPos + 3 > src.size()) {
					return bytes();
				}
				
				b1 = src[srcPos++];
				b2 = src[srcPos++];
				b3 = src[srcPos++];
				
				// 110occpp oooooooo oooooooo cccccccc
				plain = b0 & 0b00000011; //0-3
				nCopy = ((b0 & 0b00001100) << 6) + b3 + 5; //5-1028
				offset = ((b0 & 0b00010000) << 12) + (b1 << 8) + b2 + 1; //1-131072
				
			} else if(b0 < 0xFC) {
				// 111ppppp
				plain = ((b0 & 0b00011111) << 2) + 4; //4-112
				nCopy = 0;
				offset = 0;

			} else {
				// 111111pp
				plain = b0 & 0b00000011; //0-3
				nCopy = 0;
				offset = 0;
			}
			
			if(srcPos + plain > src.size() || dstPos + plain > dst.size())  {
				return bytes();
			}
			
			copyBytes(src, srcPos, dst, dstPos, plain);
			
			if(offset > dstPos || dstPos + nCopy > dst.size()) {
				return bytes();
			}
			
			//copy bytes from an earlier location in the decompressed output
			uint fromOffset = dstPos - offset;
			copyBytes(dst, fromOffset, dst, dstPos, nCopy);
		}
		
		return dst;
	}

}
