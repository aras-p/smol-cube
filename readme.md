# smol-cube: smaller Davinci/Adobe/Iridas .LUT file alternative

`.cube` LUT files are often used in photography or movie workflows, encapsulating complex color space transformations,
emulating film stock or applying a set of color grading operations. They are essentially a 3D lookup texture (LUT),
mapping three color channels (most often R, G, B) into output values. The `.cube` file format is popular within
DaVinci Resolve and Adobe tools.

See [Adobe Cube LUT Spec 1.0](https://web.archive.org/web/20220220033515/https://wwwimages2.adobe.com/content/dam/acom/en/products/speedgrade/cc/pdfs/cube-lut-specification-1.0.pdf) (2013)
and [Resolve Cafe Cube LUT info](https://resolve.cafe/developers/luts/) (2023).

| LUT                    | LUT size | File size, KB  | Zipped size, KB | Load time, ms | OCIO load time, ms |
|------------------------|---------:|---------------:|----------------:|--------------:|-------------------:|
|LUNA_COLOR.cube         | 33       |         1193   |             459 |          20.5 |               61.4 |
|LUNA_COLOR.smcube       | 33       |          421   |             246 |           0.7 |                    |
|LUNA_COLOR.smcube half4 | 33       |          281   |              79 |           0.5 |                    |
|pbrNeutral.cube         | 57       |         5367   |             759 |          91.9 |              302.5 |
|pbrNeutral.smcube       | 57       |         2170   |             197 |           4.5 |                    |
|pbrNeutral.smcube half4 | 57       |         1447   |              44 |           2.5 |                    |
