from pathlib import Path

root = Path(__file__).resolve().parents[2]
egl = (root / "src/egl/ps5_egl.c").read_text()
runtime = (root / "src/platform/ps5_agc_native_runtime.c").read_text()

assert "ps5_agc_gate2_present(surface->buffer_index,\n                                                surface->swap_interval)" in egl
assert "(!surface->window || surface->swap_interval) ?\n                           ST_FLUSH_WAIT : 0" in egl
assert "if (surface->swap_interval)\n         ps5_screen_submit_lock" in egl
assert "else\n         ps5_screen_present_lock" in egl
start = runtime.index("int ps5_agc_gate2_present")
present = runtime[start : runtime.index("\n}\n#endif", start) + 2]
assert "ps5_agc_gate2_present(unsigned buffer_index, unsigned swap_interval)" in present
assert "swap_interval == 0" in present
assert "swap_interval > 1" in present
assert "runtime_pending_batches && swap_interval != 0" in present
assert present.index("runtime_video_wait_idle()") < present.index("submit_flip(") < present.index("wait_vblank(")
print("swap interval controls asynchronous presentation")
