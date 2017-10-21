# SMB_LZ_Tool
An implementation to the SMB FF7 LZSS compression algorithm.

F-Zero GX also uses this algorithm. I just used SMB in the name because that's what I created it for.

## Support
Only decompression is currently supported

## Usage 
### Non-Command Line
Just drag the file to decompress on the executable.
### Command Line

     ./SMB_LZ_Tool [FILE...]
     
## SMB FF7 LZSS Specification
The SMB FF7 LZSS format is the same as the FF7 LZSS format, but with a slightly different header.
### FF7 LZSS Format
http://wiki.qhimm.com/view/FF7/LZS_format
### Header
The standard FF7 LZSS header is the size of the compressed file not including the header. The SMB FF7 LZSS Header is the size of the compressed file including the header. Then the size of the uncompressed file. 
