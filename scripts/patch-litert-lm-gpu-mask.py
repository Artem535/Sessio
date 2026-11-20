#!/usr/bin/env python3
"""Use LiteRT-LM's host logit mask for WebGPU constrained decoding.

LiteRT-LM 0.19's GPU mask graph rejects the tensor buffers produced by the
WebGPU executor on some devices. The host path copies/masks/writes logits and
keeps the model itself on GPU. Fail on upstream drift rather than silently
building an unpatched runtime.
"""

from pathlib import Path
import sys

source = Path(sys.argv[1]) / 'runtime/core/tasks.cc'
before = 'if (env == nullptr || accelerator == litert::HwAccelerators::kNone) {'
after = ('if (env == nullptr || accelerator == litert::HwAccelerators::kNone ||\n'
         '          settings.GetBackend() == Backend::GPU) {')
text = source.read_text(encoding='utf-8')
if text.count(after) == 1 and before not in text:
    sys.exit(0)
if text.count(before) != 1 or after in text:
    raise SystemExit(f'Unsupported LiteRT-LM constrained decoder source: {source}')
source.write_text(text.replace(before, after, 1), encoding='utf-8')
