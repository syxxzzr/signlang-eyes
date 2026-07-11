# 手语识别

[简体中文](README.md) | [English](README.en.md)

`signlang_det` 从连续双手关键点中检测完整动作，使用 RKNN 时序编码器生成逐帧特征，再通过 pooled 粗检索、Top-K DTW 重排和阈值拒识匹配动态手语库。

## 处理管线

```text
HandPoseDetection[] → FeatureExtractor → GestureSegmenter → SegmentQueue
  → SegmentPreprocessor [1,64,168] → TemporalEncoder [1,64,128]
  → pooled Top-K → DTW → 阈值与距离间隔拒识 → SignlangResult
```

- 左右手槽位固定，按上游 handedness 路由。
- 每帧每只手按腕点和最大欧氏距离归一化，速度是相邻归一化位置差。
- 12–64 帧右侧以整帧 `-100` 填充；65–120 帧覆盖完整动作重采样到 64 帧。
- SQLite 保存逐帧描述子、pooled 描述子和类别阈值。
- 实时识别与手语录入共用特征提取、质量检查、序列封装和编码管线；录入不会切滑窗。

默认模型路径为 `models/signlang/temporal_encoder.rknn`，默认原型库为 `conf/prototypes.sqlite`。不兼容 schema 会先备份再创建空库；运行时不比较数据库与模型文件版本。

参数可在 `conf/conf.toml` 的 `[signlang_det]` 中配置，包括动作分段、质量阈值、队列、Top-K、DTW、拒识和重复抑制。运行 `signlang_det --help` 可查看完整列表。

## IPC

- handpose 发布订阅输入：每个连续帧都进入分段器。
- SignlangResult 发布订阅输出：默认只发布识别成功结果，可配置发布拒识。
- 原型控制请求响应：重载和状态查询。
- 手语管理请求响应：列表、完整动作上传、替换、删除和状态查询。
