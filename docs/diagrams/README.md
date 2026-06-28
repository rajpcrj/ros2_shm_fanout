# docs/diagrams/

Generated diagrams used across the README and docs. **Do not edit the `.png` by
hand** — they are rendered from [`make_diagrams.py`](make_diagrams.py).

| file | used in | shows |
|---|---|---|
| `overview.png` | top of [README](../../README.md) | one write → shared memory → many readers |
| `dds_path.png` | [Why this beats plain ROS 2](../../README.md#why-this-beats-plain-ros-2-here--dds-path-vs-the-bypass) | normal ROS 2 message path through DDS |
| `bridge_path.png` | same section | how this library bypasses DDS |
| `pipeline.png` | [end_to_end](../../examples/end_to_end/README.md) | the 4-stage cross-machine pipeline |

Regenerate after editing the script:
```bash
python3 docs/diagrams/make_diagrams.py
```
Requires `matplotlib` (already a dev dep for the benchmark graphs).
