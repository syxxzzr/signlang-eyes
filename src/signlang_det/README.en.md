# Sign Language Recognition

[简体中文](README.md) | [English](README.en.md)

`signlang_det` detects complete actions in continuous dual-hand landmarks, generates per-frame features with an RKNN temporal encoder, then matches a dynamic gesture library using pooled coarse search, Top-K DTW reranking, and calibrated rejection.

## Pipeline

```text
HandPoseDetection[] → FeatureExtractor → GestureSegmenter → SegmentQueue
  → SegmentPreprocessor [1,64,168] → TemporalEncoder [1,64,128]
  → pooled Top-K → DTW → thresholds and distance-margin rejection → SignlangResult
```

- Left/right slots are fixed and use upstream handedness.
- Each hand is normalized around the wrist with maximum Euclidean distance; velocity uses adjacent normalized positions.
- Actions of 12–64 frames are padded with full `-100` frames; actions of 65–120 frames are resampled across their full duration.
- SQLite stores per-frame and pooled descriptors and class thresholds.
- Recognition and enrollment share feature extraction, quality evaluation, sequence packing, and encoding; enrollment never splits an action into windows.

The default model is `models/signlang/temporal_encoder.rknn`; the default prototype database is `conf/prototypes.sqlite`. An incompatible schema is backed up before an empty database is created. Runtime loading does not compare the database against the model file version.

Configure segmentation, quality, queue, Top-K, DTW, rejection, and duplicate suppression under `[signlang_det]` in `conf/conf.toml`. Run `signlang_det --help` for the complete option list.

## IPC

- Handpose publish-subscribe input: every consecutive frame enters the segmenter.
- SignlangResult publish-subscribe output: recognized results by default, optionally rejections.
- Prototype-control request-response: reload and status.
- Gesture-management request-response: list, full-action upload, replace, delete, and status.
