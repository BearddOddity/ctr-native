// The backdrop: the checkered flag the game waves across the screen while it loads (game/RaceFlag.c: a
// grid of squares on a sine ripple, each lit by its slope), here in deep blues so the menus stand out.
// It ripples slowly while idle and races while the game is being rebuilt.

const TOP = [28, 48, 148];
const BOTTOM = [6, 10, 44];

function mix(a: number[], b: number[], k: number): string {
  const c = a.map((v, i) => Math.round(v + (b[i] - v) * k));
  return `rgb(${c[0]},${c[1]},${c[2]})`;
}

function lerp(a: number[], b: number[], k: number): number[] {
  return a.map((v, i) => v + (b[i] - v) * k);
}

export class Flag {
  private ctx: CanvasRenderingContext2D;
  private phase = 0;
  private last = 0;
  private drawn = 0;
  fast = false;
  private still = matchMedia("(prefers-reduced-motion: reduce)").matches;

  constructor(private canvas: HTMLCanvasElement) {
    this.ctx = canvas.getContext("2d", { alpha: false })!;
    window.addEventListener("resize", () => this.fit());
    this.fit();
    requestAnimationFrame((t) => this.frame(t));
  }

  private fit() {
    const dpr = Math.min(devicePixelRatio || 1, 2);
    this.canvas.width = Math.round(innerWidth * dpr);
    this.canvas.height = Math.round(innerHeight * dpr);
    this.draw();
  }

  private frame(t: number) {
    const dt = this.last ? Math.min(0.1, (t - this.last) / 1000) : 0;
    this.last = t;
    if (!this.still && !document.hidden) {
      this.phase += dt * (this.fast ? 3.2 : 0.7);
      // ~30 frames a second is plenty for a backdrop
      if (t - this.drawn > 33) {
        this.drawn = t;
        this.draw();
      }
    }
    requestAnimationFrame((n) => this.frame(n));
  }

  private draw() {
    const { ctx } = this;
    const w = this.canvas.width;
    const h = this.canvas.height;
    const sq = Math.max(40, Math.round(h / 12));
    const amp = sq * 0.42;
    const cols = Math.ceil(w / sq) + 3;
    const rows = Math.ceil(h / sq) + 3;
    const at = (i: number, j: number): [number, number] => {
      const a = this.phase + i * 0.42 + j * 0.16;
      return [i * sq + amp * 0.25 * Math.cos(a), j * sq + amp * Math.sin(a)];
    };
    ctx.fillStyle = mix(TOP, BOTTOM, 0.5);
    ctx.fillRect(0, 0, w, h);
    ctx.lineWidth = 1.5;
    ctx.lineJoin = "round";
    for (let j = -2; j < rows; j++) {
      for (let i = -2; i < cols; i++) {
        const p0 = at(i, j);
        const p1 = at(i + 1, j);
        const p2 = at(i + 1, j + 1);
        const p3 = at(i, j + 1);
        // lit by the slope, as the game shades its flag
        const lit = Math.min(1, Math.max(0, 0.5 - (p1[1] - p0[1]) / (amp * 1.2)));
        const base = lerp(TOP, BOTTOM, Math.min(1, Math.max(0, (p0[1] + sq / 2) / h)));
        ctx.fillStyle = (i + j) & 1 ? mix(base, [255, 255, 255], 0.06 + 0.12 * lit) : mix(base, [0, 0, 0], 0.42 - 0.2 * lit);
        ctx.beginPath();
        ctx.moveTo(p0[0], p0[1]);
        ctx.lineTo(p1[0], p1[1]);
        ctx.lineTo(p2[0], p2[1]);
        ctx.lineTo(p3[0], p3[1]);
        ctx.closePath();
        ctx.fill();
        ctx.strokeStyle = ctx.fillStyle; // no hairline seams between the squares
        ctx.stroke();
      }
    }
  }
}
