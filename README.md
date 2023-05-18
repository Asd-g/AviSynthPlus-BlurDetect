## Description

Determines blurriness of frames by adding the relevant frame property.

Based on Marziliano, Pina, et al. "A no-reference perceptual blur metric." Allows for a block-based abbreviation. (https://infoscience.epfl.ch/record/111802/files/14%20A%20no-reference%20perceptual%20blur%20metric.pdf)

This is [a port of the FFmpeg filter blurdetect](https://ffmpeg.org/ffmpeg-filters.html#blurdetect-1).

### Requirements:

- AviSynth+ r3688 (can be downloaded from [here](https://gitlab.com/uvz/AviSynthPlus-Builds) until official release is uploaded) or later

- Microsoft VisualC++ Redistributable Package 2022 (can be downloaded from [here](https://github.com/abbodi1406/vcredist/releases))

### Usage:

```
BlurDetect(clip input, float "low", float "high", int "radius", int "block_pct", int "block_width", int "block_height", int[] "planes")
```

### Parameters:

- input\
    A clip to process.\
    It must be in 8..16-bit planar format.

- low, high\
    Set low and high threshold values used by the Canny thresholding algorithm.\
    The high threshold selects the "strong" edge pixels, which are then connected through 8-connectivity with the "weak" edge pixels selected by the low threshold.\
    `low` and `high` threshold values must be chosen in the range [0,1], and low should be lesser or equal to high.\
    Default: low = 0.0588, high = 0.1176.

- radius\
    Define the radius to search around an edge pixel for local maxima.\
    Must be between 0..100.\
    Default: 50.

- block_pct\
    Determine blurriness only for the most significant blocks, given in percentage.\
    Must be between 0..100.\
    Default: 80.

- block_width\
    Determine blurriness for blocks of width block_width.\
    If set to any value smaller 1, no blocks are used and the whole image is processed as one no matter of block_height.\
    Default: clip width.

- block_height\
    Determine blurriness for blocks of height block_height.\
    If set to any value smaller 1, no blocks are used and the whole image is processed as one no matter of block_width.\
    Default: clip height.

- planes\
    Sets which planes will be processed.\
    There will be new frame property `blurriness_...` for every processed plane.\
    Default: [0, 1, 2, 3].

### Building:

- Windows\
    Use solution files.

- Linux
    ```
    Requirements:
        - Git
        - C++17 compiler
        - CMake >= 3.16
    ```
    ```
    git clone https://github.com/Asd-g/AviSynthPlus-BlurDetect && \
    cd AviSynthPlus-BlurDetect && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make -j$(nproc) && \
    sudo make install
    ```
