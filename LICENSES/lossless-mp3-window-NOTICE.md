# MPEG analysis-window provenance

The MPEG analysis-window coefficients used by the lossless MP3 hybrid probe
are derived from `ff_mpa_enwindow` in FFmpeg n7.0,
`libavcodec/mpegaudiodsp_data.c`:

https://github.com/FFmpeg/FFmpeg/blob/n7.0/libavcodec/mpegaudiodsp_data.c

That file identifies itself as part of FFmpeg and is licensed under GNU LGPL
version 2.1 or, at the recipient's option, any later version. Its notice is
retained with the coefficient source. The accompanying license text is
`FFmpeg-LGPL-2.1-or-later.txt`, copied from the project's existing FFmpeg
dependency copyright file.

The probe's arithmetic and tests were written for AgPlayer. It uses the existing
FFmpeg transform dependency. This provenance record does not certify an audio
source, algorithm accuracy, or completion of a distribution compliance review.
