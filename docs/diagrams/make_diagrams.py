#!/usr/bin/env python3
"""
make_diagrams.py — render the README/doc diagrams as polished PNGs.
Pure matplotlib (no extra deps). Re-run after editing:  python3 make_diagrams.py
Outputs into this folder: overview.png, dds_path.png, bridge_path.png, pipeline.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

HERE = os.path.dirname(os.path.abspath(__file__))

# palette
C_BRIDGE = "#1b9e77"
C_BRIDGE_L = "#d3efe6"
C_DDS = "#d95f02"
C_DDS_L = "#fbe2cf"
C_NEUTRAL = "#34495e"
C_NEUTRAL_L = "#e8edf1"
C_SHM = "#7570b3"
C_SHM_L = "#e3e1f0"
C_NET = "#e7298a"
C_NET_L = "#f8dcec"
C_TXT = "#1c1c1c"


def box(ax, x, y, w, h, text, face, edge, fontsize=11, bold=False, text_color=C_TXT):
    ax.add_patch(FancyBboxPatch((x, y), w, h,
                 boxstyle="round,pad=0.02,rounding_size=0.08",
                 linewidth=1.6, edgecolor=edge, facecolor=face, zorder=2))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
            fontsize=fontsize, color=text_color,
            fontweight="bold" if bold else "normal", zorder=3, wrap=True)


def arrow(ax, x1, y1, x2, y2, color=C_NEUTRAL, lw=2.2, style="-|>", ls="-"):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2),
                 arrowstyle=style, mutation_scale=18, linewidth=lw,
                 color=color, linestyle=ls, zorder=1,
                 shrinkA=2, shrinkB=2))


def finish(ax, xlim, ylim):
    ax.set_xlim(*xlim); ax.set_ylim(*ylim)
    ax.axis("off"); ax.set_aspect("equal")


def save(fig, name):
    p = os.path.join(HERE, name)
    fig.savefig(p, dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", p)


# ───────────────────────── 1) overview ─────────────────────────
def overview():
    fig, ax = plt.subplots(figsize=(11, 3.4))
    box(ax, 0.3, 1.2, 2.4, 1.0, "PRODUCER\n(camera / ROS node /\nyour code)", C_NEUTRAL_L, C_NEUTRAL, 10, True)
    box(ax, 3.6, 1.0, 3.4, 1.4,
        "/dev/shm/<stream>_*\nheader  +  frame  +  recipe.json\n(seqlock + futex)",
        C_SHM_L, C_SHM, 10, True)
    box(ax, 8.0, 2.25, 2.7, 0.8, "READER 1  (C++)", C_BRIDGE_L, C_BRIDGE, 10)
    box(ax, 8.0, 1.30, 2.7, 0.8, "READER 2  (Python)", C_BRIDGE_L, C_BRIDGE, 10)
    box(ax, 8.0, 0.35, 2.7, 0.8, "READER N  (no ROS)", C_BRIDGE_L, C_BRIDGE, 10)
    arrow(ax, 2.7, 1.7, 3.6, 1.7, C_NEUTRAL, 2.6)
    ax.text(3.15, 1.95, "1 write\n1 copy", ha="center", va="bottom", fontsize=8, color=C_NEUTRAL)
    for yy in (2.65, 1.70, 0.75):
        arrow(ax, 7.0, 1.7, 8.0, yy, C_BRIDGE, 2.0)
    ax.text(7.5, 0.15, "writer's notify = one FUTEX_WAKE for all readers (O(1) wake; "
            "RAM O(1), delivery CPU O(N) low-slope)",
            ha="center", fontsize=7.8, color=C_BRIDGE, style="italic")
    ax.set_title("Overview — one write into shared memory, many zero-copy readers",
                 fontsize=12, fontweight="bold", color=C_TXT)
    finish(ax, (0, 11), (0, 3.2))
    save(fig, "overview.png")


# ───────────────── 2) normal ROS 2 (DDS) path ─────────────────
def dds_path():
    fig, ax = plt.subplots(figsize=(11, 5.6))
    # publisher column
    box(ax, 0.3, 6.0, 4.3, 0.7, "PUBLISHER process", C_DDS_L, C_DDS, 11, True)
    steps_pub = [
        ("1.  your typed message object", 5.1),
        ("2.  rclcpp → rmw → DDS", 4.3),
        ("3.  SERIALIZE whole msg → CDR bytes", 3.5),
        ("4.  DDS transport: kernel copy\n     PER subscriber (loopback)", 2.5),
    ]
    for t, y in steps_pub:
        hot = "SERIALIZE" in t or "PER subscriber" in t
        box(ax, 0.3, y, 4.3, 0.75 if "\n" not in t else 0.95, t,
            "#f6c9a4" if hot else "white", C_DDS, 9.5, hot)
    # subscriber column
    box(ax, 6.4, 6.0, 4.3, 0.7, "each SUBSCRIBER process", C_DDS_L, C_DDS, 11, True)
    steps_sub = [
        ("6.  DDS receive + reassemble", 4.3),
        ("7.  DESERIALIZE CDR → typed msg", 3.5),
        ("8.  copy into subscription queue", 2.7),
        ("9.  executor wakes → your callback", 1.9),
    ]
    for t, y in steps_sub:
        hot = "DESERIALIZE" in t
        box(ax, 6.4, y, 4.3, 0.75, t, "#f6c9a4" if hot else "white", C_DDS, 9.5, hot)
    # the wire
    box(ax, 4.85, 2.7, 1.5, 0.8, "wire /\nbuffer", C_NEUTRAL_L, C_NEUTRAL, 9, True)
    arrow(ax, 4.6, 2.9, 4.85, 3.0, C_DDS, 2.0)
    arrow(ax, 6.35, 3.0, 6.4, 4.0, C_DDS, 2.0)
    ax.text(5.6, 3.65, "5. bytes\non the wire", ha="center", fontsize=8, color=C_DDS)
    # cost callout
    ax.text(5.5, 0.9,
            "Costs that grow with data & subscribers:  serialize + deserialize EVERY msg  ·  "
            "a copy PER subscriber (O(n))\ndiscovery / QoS / RTPS bookkeeping  ·  one executor wakeup per subscriber",
            ha="center", va="center", fontsize=9, color=C_DDS,
            bbox=dict(boxstyle="round,pad=0.4", fc=C_DDS_L, ec=C_DDS))
    ax.set_title("Normal ROS 2 path — every message goes through the DDS middleware",
                 fontsize=12, fontweight="bold", color=C_TXT)
    finish(ax, (0, 11), (0.3, 6.9))
    save(fig, "dds_path.png")


# ───────────────── 3) the bridge bypass path ─────────────────
def bridge_path():
    fig, ax = plt.subplots(figsize=(11, 5.2))
    box(ax, 0.3, 5.4, 4.3, 0.7, "WRITER", C_BRIDGE_L, C_BRIDGE, 12, True)
    steps_w = [
        ("1.  your bytes (already in RAM)", 4.5),
        ("2.  ONE memcpy → /dev/shm frame\n     (no serialize for FLAT)", 3.4),
        ("3.  bump seqlock, set 64B header", 2.5),
        ("4.  ONE futex WAKE (all readers)", 1.6),
    ]
    for t, y in steps_w:
        hot = "ONE memcpy" in t or "ONE futex" in t
        box(ax, 0.3, y, 4.3, 0.95 if "\n" in t else 0.75, t,
            C_BRIDGE_L if hot else "white", C_BRIDGE, 9.5, hot)
    # shared memory in the middle
    box(ax, 4.9, 2.6, 1.4, 1.6, "/dev/shm\nframe\n(mmap'd)", C_SHM_L, C_SHM, 9.5, True)
    box(ax, 6.6, 5.4, 4.1, 0.7, "each READER (any process)", C_BRIDGE_L, C_BRIDGE, 12, True)
    steps_r = [
        ("5.  was FUTEX_WAIT (0% CPU)", 4.5),
        ("6.  read seqlock; frame already\n     mapped in this process", 3.4),
        ("7.  ONE memcpy out (or use in place)", 2.5),
    ]
    for t, y in steps_r:
        box(ax, 6.6, y, 4.1, 0.95 if "\n" in t else 0.75, t, "white", C_BRIDGE, 9.5)
    arrow(ax, 4.6, 3.6, 4.9, 3.5, C_BRIDGE, 2.4)             # writer -> shm
    arrow(ax, 6.3, 3.5, 6.6, 3.6, C_BRIDGE, 2.4)             # shm -> reader (map)
    arrow(ax, 4.6, 1.95, 6.6, 4.7, C_BRIDGE, 2.0, ls="--")   # futex wake
    ax.text(5.6, 1.5, "one syscall\nwakes ALL", ha="center", fontsize=8,
            color=C_BRIDGE, style="italic")
    ax.text(5.5, 0.75,
            "Removed vs DDS:  serialize/deserialize (FLAT)  ·  per-subscriber copies → ONE shared copy (RAM is O(1))\n"
            "discovery / QoS / RTPS  ·  busy-poll → 0% CPU sleep   |   CPU is still O(N) readers, just a ~8x smaller constant",
            ha="center", va="center", fontsize=8.5, color=C_BRIDGE,
            bbox=dict(boxstyle="round,pad=0.4", fc=C_BRIDGE_L, ec=C_BRIDGE))
    ax.set_title("This bridge — bypass DDS: one write, one wake, readers map the same bytes",
                 fontsize=12, fontweight="bold", color=C_TXT)
    finish(ax, (0, 11), (0.3, 6.3))
    save(fig, "bridge_path.png")


# ───────────────── 4) end-to-end pipeline ─────────────────
def pipeline():
    fig, ax = plt.subplots(figsize=(11.5, 4.6))
    # machine A
    ax.add_patch(FancyBboxPatch((0.2, 0.4), 4.4, 3.6, boxstyle="round,pad=0.03",
                 lw=1.8, ec=C_NEUTRAL, fc="#f7f9fa", zorder=0))
    ax.text(2.4, 3.75, "MACHINE A  (source)", ha="center", fontsize=11, fontweight="bold", color=C_NEUTRAL)
    box(ax, 0.5, 2.9, 3.8, 0.7, "ROS 2 graph (all topics)", C_NEUTRAL_L, C_NEUTRAL, 9.5)
    box(ax, 0.5, 1.9, 3.8, 0.7, "[1] e2e_1_ros2_to_shm", C_BRIDGE_L, C_BRIDGE, 9.5, True)
    box(ax, 0.5, 0.9, 3.8, 0.7, "[2] e2e_2_shm_to_udp", C_NET_L, C_NET, 9.5, True)
    arrow(ax, 2.4, 2.9, 2.4, 2.6, C_BRIDGE)
    arrow(ax, 2.4, 1.9, 2.4, 1.6, C_NET)
    # machine B
    ax.add_patch(FancyBboxPatch((6.9, 0.4), 4.4, 3.6, boxstyle="round,pad=0.03",
                 lw=1.8, ec=C_NEUTRAL, fc="#f7f9fa", zorder=0))
    ax.text(9.1, 3.75, "MACHINE B  (destination)", ha="center", fontsize=11, fontweight="bold", color=C_NEUTRAL)
    box(ax, 7.2, 2.9, 3.8, 0.7, "ROS 2 graph (same topics)", C_NEUTRAL_L, C_NEUTRAL, 9.5)
    box(ax, 7.2, 1.9, 3.8, 0.7, "[4] e2e_4_shm_to_ros2", C_BRIDGE_L, C_BRIDGE, 9.5, True)
    box(ax, 7.2, 0.9, 3.8, 0.7, "[3] e2e_3_udp_to_shm", C_NET_L, C_NET, 9.5, True)
    arrow(ax, 9.1, 1.6, 9.1, 1.9, C_BRIDGE)
    arrow(ax, 9.1, 2.6, 9.1, 2.9, C_BRIDGE)
    # network hop
    arrow(ax, 4.6, 1.25, 7.2, 1.25, C_NET, 2.6)
    ax.text(5.9, 1.5, "UDP  (topic + type carried inline)", ha="center",
            fontsize=9, color=C_NET, fontweight="bold")
    ax.text(5.9, 0.95, "ssh / two machines", ha="center", fontsize=8,
            color=C_NEUTRAL, style="italic")
    ax.set_title("End-to-end — all ROS 2 topics from A reappear on B, as if nothing happened",
                 fontsize=12, fontweight="bold", color=C_TXT)
    finish(ax, (0, 11.5), (0.3, 4.2))
    save(fig, "pipeline.png")


if __name__ == "__main__":
    overview(); dds_path(); bridge_path(); pipeline()
