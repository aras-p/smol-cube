# smol-cube: smaller Resolve/Adobe/Iridas .LUT file alternative

`.cube` LUT files are often used in photography or movie workflows, encapsulating complex color space transformations,
emulating film stock or applying a set of color grading operations. They are essentially a 3D lookup texture (LUT),
mapping three color channels (most often R, G, B) into output values. The `.cube` file format is popular within
DaVinci Resolve and Adobe tools.

See [Adobe Cube LUT Spec 1.0](https://web.archive.org/web/20220220033515/https://wwwimages2.adobe.com/content/dam/acom/en/products/speedgrade/cc/pdfs/cube-lut-specification-1.0.pdf) (2013)
and [Resolve Cafe Cube LUT info](https://resolve.cafe/developers/luts/) (2023).

However, the `.cube` file format is purely **text based**. The whole 3D lookup texture is several hundred thousand
or several million floating pointer numbers is ASCII format!

> Most other LUT formats are also text based: Houdini .hdl, Imageworks .spi3d, TrueLight .cub, Cinespace .csp, Discreet Flame .3dl, Inventor .vf.
> Academy/ASC Common LUT Format (.clf) and Autodesk Color Transform Format (.ctf) are XML, even! Among them, only Houdini .hdl has a "binary form" that,
> curiously, is big-endian.

### smol-cube: load and save LUT files from ASCII .cube, or a small binary format

This library provides a simple binary `.smcube` format. It has the following features:

- 1D, 2D or 3D LUTs.
- Float (32 bit) or half (16 bit) precision data.
- Three channel (RGB) or four channel (RGB, plus unused alpha) options. The latter is for faster loading into GPU APIs,
  since they usually do not have a three-channel texture formats.
- Data can be losslessly filtered to make it more *compressible*. This does not alter the file data size itself,
  but if you will further compress the file with zlib/zstd/lz4 or any other compressor, it will be *more smoller*.
  See [my blog post](https://aras-p.info/blog/2023/03/01/Float-Compression-7-More-Filtering-Optimization/) or
  [Blosc bytedelta](https://www.blosc.org/posts/bytedelta-enhance-compression-toolset/) for details.
- Similar to Resolve .cube format, it can have multiple LUTs (e.g. 1D shaper LUT followed by 3D LUT).

Compared to a text based file format, a binary format usually takes up less space. Text based formats do compress
pretty well, however the lossless data filter used in smol-cube makes it more compressible. Additionally,
smol-cube can convert the LUT data into half-precision floats, since many applications (e.g. game engines like Unity)
use half-precision LUTs at runtime anyway.

![](/doc/chart-pbrneutral-size.png)

Taking Khronos [PBR Neutral LUT](https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral) as an example,
the raw .cube file size is 5.4MB. Even if we compress that with [zstd](http://www.zstd.net/) level 10, it is still 543 KB. With smol-cube,
half precision and zstd level 10, that becomes only 27 KB.

![](/doc/chart-pbrneutral-loadtime.png)

Likewise, loading a simple binary format is much faster than loading from a text-based file. This library can
load the text based .cube files too, and takes 55ms to load the Khronos PBR Neutral file (for comparison,
[OpenColorIO](https://github.com/AcademySoftwareFoundation/OpenColorIO) 2.3
is even slower at loading this file: 302ms). However the load time can go down to 2ms for a .smcube half precision
format. (the times are for loading the file *and* creating a GPU 3D texture with that data, on D3D11).

### smol-cube C++ library

The library itself is written in C++, and requires C++ 17 or later. It provides functions for:

- Loading LUT(s) from `.cube` or `.smcube` files: `smcube_load_from_file`.
- Saving LUT(s) into `.smcube` file: `smcube_save_to_file_smcube`. This can convert the float32 data down into float16,
  and can also expand from RGB to four-channel RGBX.
- Saving LUT(s) into `.cube` file: `smcube_save_to_file_resolve_cube`. Note that this is limited to what Resolve .cube format
  can do, i.e. only 32 bit float data, only 3 channels, and the file can contain one 1D LUT, one 3D LUT, or one 1D + one 3D LUT only.
- Access and inspection of the loaded LUT data.

In order to use the library, compile `src/smol_cube.cpp` in your project, and include `src/smol_cube.h`.

License is either MIT or Unlicense, whichever is more convenient for you.

### smol-cube-conv command line tool

`smol-cube-conv` command line utility converts Resolve/Adobe `.cube` files into `.smcube` binary format.

    smol-cube-conv [flags] <input file> ...

Without extra arguments, this will convert given input .cube file(s) into .smcube files
with lossless data filtering (making them more compressible), and keeping the data
in full Float32 precision. Optional flags:

* `--float16` convert data into Float16 (half precision floats)
* `--rgba` expand data from RGB to RGB(A) (A being unused)
* `--nofilter` do not perform data filtering to improve compressability


### smol-cube-viewer app

Tiny viewer that loads several pictures from under `tests/` folder and displays them using LUTs found under `tests/luts/` folder.
Uses [Sokol libraries](https://github.com/floooh/sokol) for graphics/application and
[stb_image.h](https://github.com/nothings/stb/blob/master/stb_image.h) for loading the photos.

The pictures are displayed and the LUTs applied using a GPU 3D texture, using D3D11 (Windows), Metal (macOS)
or OpenGL (Linux - untested).

![](/doc/shot-viewer.jpg)

Left/Right keys switch between LUTs, Up/Down adjusts LUT application intensity. Space reloads the current LUT.

The viewer assumes that the LUTs are meant for low dynamic range color grading, directly on sRGB color values.
Some other LUTs might not be meant for that (e.g. Khronos PBR Neutral) and while they will "work", the results
will not look pleasant.
