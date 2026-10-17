# libmad 来源

仓库：https://github.com/sezero/libmad

锁定提交：`486f902c6c686eafced3450851849527e29bc7f6`。

许可证：GPL version 2 或后续版本，见 `libmad/COPYING`；原始版权标注保留。

源码改动有两处，均以补丁记录：

- `synth-pointer.patch`：四处合成滤波器先构造数组前方指针、再用正下标补回的表达式，改成在有效数组基址上使用等价下标。避免越界指针构造；主机 ASan/UBSan 测试已通过，改动前后测试 MP3 的 PCM 校验值相同。
- `layer3-work.patch`（0.9.5）：`III_decode()` 的 4.6 KB 重量化工作数组 `xr[2][576]` 从栈上改为静态缓冲 `III_xr_work`，由指针 `mad_layer_III_xr`（`layer3.h` 导出）引用；定义 `MAD_WORK_XYRAM` 时该缓冲放进 gint 的 `.xyram` 段（片上 X/Y RAM）。目的是让每帧的清零与覆盖写不经过外部总线（见 NOISE.md 第 6 项）。代价：`III_decode()` 不再可重入（本项目单线程）。改动前后主机解码输出逐字节相同（`tests/test_mp3.py`、`tests/test-live-stream.sh`）。

使用 `FPM_64BIT`、整数解码、`-fwrapv`，没有使用浮点或 DSP 专用汇编。上游定点代码包含有符号移位惯用法，主机测试对上游禁用 shift sanitizer，仍启用地址及其他未定义行为检查；PoC 自有代码启用完整 ASan/UBSan。
