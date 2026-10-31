Source: https://github.com/mackron/dr_libs
Commit: dfe8377631000664666519fdb83da193fd8037f4
File: dr_flac.h
License: public domain or MIT-0 (see end of header).

本地补丁（32 位 PCM）：
- 按 [RFC 9639 9.1.6](https://www.rfc-editor.org/rfc/rfc9639.html#section-9.1.6) 接受帧头位深编码 0b111。
- 定义 DR_FLAC_32BIT_SIDE_BITS 时，支持 32 位立体声产生的 33 位差分子帧及 64 位声道重建。
- 差分样本低 32 位沿用原工作区，额外一位由调用方提供位图；当前支持最多 4608 帧。
- 未定义上述宏时仍拒绝无法存入原缓冲的 33 位差分子帧。
- 定义 DR_FLAC_PREDICTION_LOOP 时，64 位 LPC 预测改用整数乘加循环，便于 GCC/ARM 生成紧凑的 SMLAL 循环，保留逐位精度。
