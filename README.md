#### Sims 2 Compression Experiments

<img src="https://github.com/lingeringwillx/CrappySims2Compression/assets/111698406/5e1e045d-ab02-48c0-9a69-f8fb5ab57cbc" width="400">

<br/>Current build can be compiled with the nuwen version of MinGW. Run `compile.bat` to compile.

Usage: `dbpf-recompress -args package_file_or_folder`

There is now an experimental release that could be used as a drop-in replacement for The Compressorizer's original executable. It achieves faster compression in the following ways:

1- By utilizing all of the cores of the CPU for compression.

2- By using zlib's level 5 compression parameters instead of level 9.

To use it, just download the .exe file and put it in the same directory as The Compressorizer, overwriting the old file.

Alternatively, you can just drag and drop your file or folder to the executable and it will be compressed.

[Refpack/QFS compression Information Repository](https://github.com/lingeringwillx/Refpack-QFS-Resources/tree/main)
