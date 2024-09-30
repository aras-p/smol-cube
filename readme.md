# smol-cube: smaller Davinci/Adobe/Iridas .LUT file alternative

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

### smol-cube C++ library

The library itself is written in C++, and requires C++ 17 or later. It provides functions for:

- Loading LUT(s) from `.cube` or `.smcube` files: `smcube_luts_load_from_file`.
- Saving LUT(s) into `.smcube` file: `smcube_luts_save_to_file_smcube`. This can convert the float32 data down into float16,
  and can also expand from RGB to four-channel RGBX.
- Saving LUT(s) into `.cube` file: `smcube_luts_save_to_file_resolve_cube`. Note that this is limited to what Resolve .cube format
  can do, i.e. only 32 bit float data, only 3 channels, and the file can contain one 1D LUT, one 3D LUT, or one 1D + one 3D LUT only.
- Access and inspection of the loaded LUT data.

In order to use the library, compile `src/smol_cube.cpp` in your project, and include `src/smol_cube.h`.

### smol-cube-conv command line tool

`smol-cube-conv` command line utility converts Resolve/Adobe `.cube` files into `.smcube` binary format.

### smol-cube-viewer app

`smol-cube-viewer` TBD.




| LUT                    | LUT size | File size, KB  | Zipped size, KB | Load time, ms | OCIO load time, ms |
|------------------------|---------:|---------------:|----------------:|--------------:|-------------------:|
|LUNA_COLOR.cube         | 33       |         1193   |             459 |          11.7 |               61.4 |
|LUNA_COLOR.smcube       | 33       |          421   |             246 |           0.7 |                    |
|LUNA_COLOR.smcube half4 | 33       |          281   |              79 |           0.5 |                    |
|pbrNeutral.cube         | 57       |         5367   |             759 |          54.7 |              302.5 |
|pbrNeutral.smcube       | 57       |         2170   |             197 |           4.5 |                    |
|pbrNeutral.smcube half4 | 57       |         1447   |              44 |           2.5 |                    |
