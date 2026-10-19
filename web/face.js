// MUMU's face for the browser preview — a port of src/face.cpp.
// Keep in step with the firmware when changing an animation.
"use strict";
const MumuFace = (() => {
  const EYE_Y = 98, EYE_DX = 47, EYE_R = 35, MOUTH_Y = 172, PI = Math.PI;
  const PALETTES = [
    ["Slime green", [112, 201, 86], [84, 160, 62]],
    ["Grape", [150, 100, 210], [118, 74, 176]],
    ["Tangerine", [255, 150, 60], [222, 116, 38]],
    ["Sky blue", [80, 170, 240], [52, 132, 204]],
    ["Bubblegum", [255, 120, 180], [222, 86, 148]],
    ["Sunny", [250, 210, 60], [214, 172, 34]],
  ];
  const rgb = (r, g, b) => `rgb(${r|0},${g|0},${b|0})`;
  const clamp = (v, lo, hi) => v < lo ? lo : (v > hi ? hi : v);
  const approach = (cur, tgt, sp, dt) => cur + (tgt - cur) * (1 - Math.exp(-sp * dt));
  const frand = (a, b) => a + (b - a) * Math.random();
  const INK = rgb(40, 30, 60), MOUTH = rgb(70, 20, 40), TONGUE = rgb(255, 110, 130), WHITE = "#fff";
  const SOUND_REACTIONS = ["laugh","gasp","silly","love","dizzy","angry","scared","sing","cool","sneeze","sad","party","kiss","toot"];

  function rainbow(h) {
    h = ((h % 1) + 1) % 1 * 6;
    const i = Math.floor(h), f = h - i;
    let r = 0, g = 0, b = 0;
    switch (i) {
      case 0: r = 1; g = f; break; case 1: r = 1 - f; g = 1; break;
      case 2: g = 1; b = f; break; case 3: g = 1 - f; b = 1; break;
      case 4: r = f; b = 1; break; default: r = 1; b = 1 - f;
    }
    return rgb(60 + r * 195, 60 + g * 195, 60 + b * 195);
  }

  class Face {
    constructor(canvas, name = "MUMU") {
      this.cv = canvas; this.c = canvas.getContext("2d");
      this.hello = `Hi! I'm ${name}`;
      this.setColor(0);
      this.cur = this.params(); this.cur.mouthCurve = 0.6; this.tgt = { ...this.cur };
      this.eyeStyle = ["normal", "normal"]; this.mouthStyle = "normal";
      this.react_ = null; this.rt = 0; this.rdur = 0; this.rkey = -1;
      this.audio = false; this.talk = 0; this.time = 0;
      this.blinkT = 0; this.nextBlink = 2; this.blinkDur = 0.16; this.doubleBlink = false;
      this.nextSaccade = 1; this.nextIdle = 12; this.idleEnabled = true;
      this.bounce = 0; this.shake = 0; this.sway = 0; this.tremble = 0; this.lift = 0; this.spiralRot = 0;
      this.sparkTimer = 0; this.hat = 0; this.glanceKey = -1; this.glanceT = 0;
      this.p = [];
    }
    params() {
      return { eyeOpenL: 1, eyeOpenR: 1, eyeScale: 1, pupilX: 0, pupilY: 0, pupilSize: 1, pupilCross: 0,
               browH: 0, browTilt: 0, browAsym: 0, mouthOpen: 0, mouthWidth: 1, mouthCurve: 0.55, blush: 0 };
    }
    setColor(i) {
      const p = PALETTES[((i % PALETTES.length) + PALETTES.length) % PALETTES.length];
      this.body = rgb(...p[1]); this.bodyDark = rgb(...p[2]);
      this.blushC = (i % PALETTES.length === 4) ? rgb(255, 80, 130) : rgb(255, 140, 170);
    }
    busy() { return this.react_ !== null; }
    glance(k) { this.glanceKey = k; this.glanceT = 0.35; }

    react(r, key = -1) {
      this.react_ = r; this.rt = 0; this.rkey = key; this.sparkTimer = 0;
      this.eyeStyle = ["normal", "normal"]; this.mouthStyle = "normal"; this.hat = 0;
      const yellow = rgb(255, 225, 60);
      const D = { laugh: 1.4, gasp: 1.3, silly: 1.5, love: 1.7, dizzy: 1.8, angry: 1.6, scared: 1.8, sing: 2.4,
        cool: 1.8, sneeze: 2.0, sad: 2.2, party: 2.0, kiss: 1.5, toot: 2.0, mash: 2.2, hello: 2.2,
        yawn: 2.4, whistle: 2.2, look: 2.0, poke: 1.0 };
      this.rdur = D[r] || 1.0;
      if (r === "gasp") { this.shake = 7; this.spawn("text", 120, 22, 0, -10, 1.1, 1.6, rgb(230, 40, 40), "!"); }
      if (r === "dizzy") for (let i = 0; i < 4; i++) this.spawn("orbit", 0, 0, 0, 0, 1.8, 9, yellow);
      if (r === "angry") this.shake = 5;
      if (r === "poke") { this.shake = 4; if (this.pokeEye === undefined) this.pokeEye = 0; }
      if (r === "cool") this.spawn("sparkle", 150, 80, 0, 0, 0.6, 12, WHITE);
      if (r === "party") { this.hat = 1; this.bounce = 8; }
      if (r === "mash") { this.shake = 9; for (let i = 0; i < 5; i++) this.spawn("orbit", 0, 0, 0, 0, 2.2, 10, yellow);
                          this.spawn("text", 120, 222, 0, -6, 1.8, 1.0, WHITE, "WHOA!"); }
      if (r === "hello") this.spawn("bubble", 120, 26, 0, 0, 2.0, 1, WHITE, this.hello);
      this.p = this.p.filter(q => q.type !== "orbit" || q.life === q.maxLife);
      const orb = this.p.filter(q => q.type === "orbit");
      orb.forEach((q, k) => q.angle = k * 2 * PI / orb.length);
    }

    spawn(type, x, y, vx, vy, life, size, color, text) {
      if (this.p.length < 40) this.p.push({ type, x, y, vx, vy, life, maxLife: life, size, angle: 0, color, text });
    }

    updateParticles(dt) {
      this.p.forEach((q, i) => {
        q.life -= dt;
        if (q.type === "orbit") { q.angle += dt * 5; return; }
        q.x += q.vx * dt; q.y += q.vy * dt;
        if (q.type === "heart" || q.type === "note") q.x += Math.sin((this.time + i) * 4) * 12 * dt;
        if (q.type === "sweat" || q.type === "tear" || q.type === "drop") q.vy += 160 * dt;
        if (q.type === "confetti") { q.vy += 60 * dt; q.x += Math.sin((this.time + i) * 6) * 30 * dt; }
        if (q.type === "puff") { q.size += 14 * dt; q.vx *= 0.97; q.vy *= 0.97; }
        q.angle += dt * 3;
      });
      this.p = this.p.filter(q => q.life > 0);
    }

    idle(dt) {
      this.blinkT -= dt; this.nextBlink -= dt;
      if (this.nextBlink <= 0) {
        this.blinkDur = 0.16; this.blinkT = this.blinkDur;
        if (this.doubleBlink) { this.nextBlink = 0.22; this.doubleBlink = false; }
        else { this.nextBlink = frand(1.8, 5.5); this.doubleBlink = Math.random() < 0.18; }
      }
      this.nextSaccade -= dt;
      if (this.nextSaccade <= 0 && !this.react_) {
        this.nextSaccade = frand(0.7, 2.6);
        if (Math.random() < 0.35) { this.tgt.pupilX = 0; this.tgt.pupilY = 0; }
        else { this.tgt.pupilX = frand(-0.9, 0.9); this.tgt.pupilY = frand(-0.6, 0.7); }
      }
      if (this.idleEnabled && !this.react_ && !this.audio) {
        this.nextIdle -= dt;
        if (this.nextIdle <= 0) { this.nextIdle = frand(9, 22); const r = Math.random();
          this.react(r < 0.3 ? "yawn" : (r < 0.65 ? "whistle" : "look")); }
      }
    }

    applyReaction(dt) {
      const R = this.react_; if (!R) return;
      this.rt += dt;
      const t = this.rt, u = clamp(t / this.rdur, 0, 1), g = this.tgt, es = this.eyeStyle;
      if (this.rkey >= 0 && t < 0.35) { g.pupilX = (this.rkey - 2) * 0.45; g.pupilY = 0.8; }
      const crossed = (at) => t - dt < at && t >= at;
      switch (R) {
        case "laugh":
          es[0] = es[1] = "happy";
          g.mouthOpen = 0.75 + 0.25 * Math.abs(Math.sin(t * 14)); g.mouthCurve = 1; g.mouthWidth = 1.15;
          g.browH = 0.6; g.blush = 0.6; this.bounce = 9;
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t < 1.1) { this.sparkTimer = 0.35; const left = Math.floor(t * 3) % 2;
            this.spawn("text", left ? 44 : 196, 180, left ? -18 : 18, -60, 1.0, 0.75, WHITE, "HA"); }
          break;
        case "gasp":
          g.eyeScale = 1.25; g.pupilSize = 0.45; g.browH = 1; g.browTilt = 0.2;
          this.mouthStyle = "o"; g.mouthOpen = 0.8; g.mouthWidth = 0.55;
          if (t > 0.35) { g.pupilX = 0; g.pupilY = 0; }
          break;
        case "silly":
          g.pupilCross = 1; g.pupilX = 0; g.pupilY = 0.15; g.pupilSize = 0.75;
          this.mouthStyle = "tongue"; g.mouthCurve = 0.8; g.browH = 0.5; g.browTilt = -0.4 + 0.8 * Math.sin(t * 10);
          this.sway = 6 * Math.sin(t * 9);
          break;
        case "love":
          es[0] = es[1] = "heart";
          g.blush = 1; g.mouthCurve = 1; g.mouthOpen = 0.25; g.mouthWidth = 0.8; g.browH = 0.4; g.browTilt = 0.5;
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t < 1.3) { this.sparkTimer = 0.18;
            this.spawn("heart", frand(30, 210), 230, frand(-15, 15), frand(-90, -60), 1.6, frand(7, 13), rgb(255, 60, 110)); }
          break;
        case "dizzy": case "mash":
          es[0] = es[1] = "spiral";
          this.spiralRot += dt * (R === "mash" ? 14 : 9);
          this.mouthStyle = "wavy"; g.mouthWidth = 0.8; g.browTilt = 0.6 * Math.sin(t * 6);
          this.sway = (R === "mash" ? 12 : 8) * Math.sin(t * 5);
          break;
        case "angry":
          g.browTilt = -1.2; g.browH = -0.5; g.eyeOpenL = g.eyeOpenR = 0.72; g.pupilSize = 0.8;
          g.mouthCurve = -0.9; g.mouthOpen = 0.32 + 0.08 * Math.sin(t * 20); g.mouthWidth = 0.9;
          if (t > 0.35) { g.pupilX = 0; g.pupilY = 0.1; }
          if (t % 0.6 < dt) this.shake = 4;
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t < 1.4) { this.sparkTimer = 0.22; const left = Math.floor(t * 5) % 2;
            this.spawn("puff", left ? 22 : 218, 70, left ? -10 : 10, -55, 0.9, 7, rgb(240, 240, 250)); }
          break;
        case "scared":
          g.eyeScale = 1.15; g.pupilSize = 0.38; g.browH = 0.8; g.browTilt = 1.1;
          this.mouthStyle = "wavy"; g.mouthWidth = 0.55;
          g.pupilX = 0.25 * Math.sin(t * 17); g.pupilY = -0.1; this.tremble = 2.2;
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t < 1.5) { this.sparkTimer = 0.4;
            this.spawn("sweat", Math.random() < 0.5 ? 36 : 204, 56, 0, 0, 1.0, 6, rgb(120, 200, 255)); }
          break;
        case "sing":
          es[0] = es[1] = "happy";
          this.mouthStyle = "o"; g.mouthOpen = 0.3; g.mouthWidth = 0.5; g.browH = 0.7; g.blush = 0.4;
          this.sway = 7 * Math.sin(t * 4.2);
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0) { this.sparkTimer = 0.42; const left = Math.floor(t * 2.4) % 2;
            this.spawn("note", left ? 60 : 180, 200, left ? -25 : 25, frand(-60, -45), 1.6, frand(8, 11), rainbow(Math.random())); }
          break;
        case "cool":
          es[0] = es[1] = "shades";
          g.mouthCurve = 0.6; g.mouthWidth = 0.75; g.browAsym = 0.9; g.browH = 0.2;
          this.sway = 3 * Math.sin(t * 3);
          break;
        case "sneeze":
          if (t < 0.95) { const k = t / 0.95;
            g.eyeOpenL = g.eyeOpenR = 1 - 0.75 * k; g.browH = 0.9 * k; g.browTilt = 0.6 * k;
            this.mouthStyle = "o"; g.mouthOpen = 0.2 + 0.6 * k; g.mouthWidth = 0.45; this.lift = -10 * k;
          } else if (t < 1.45) {
            if (t - dt < 0.95) {
              this.shake = 10; this.spawn("text", 120, 30, 0, -8, 1.1, 1.05, WHITE, "ACHOO!");
              for (let i = 0; i < 9; i++) this.spawn("drop", 120 + frand(-30, 30), 185, frand(-140, 140), frand(-60, 40), 0.7, frand(2.5, 4.5), rgb(150, 215, 255));
            }
            es[0] = es[1] = "closed";
            g.mouthOpen = 0.9; g.mouthWidth = 1.1; g.mouthCurve = 0.2; g.browH = -0.3; this.lift = 8;
          } else { g.mouthCurve = 0.6; g.blush = 0.5; this.lift = 0; }
          break;
        case "sad":
          g.browTilt = 1.1; g.browH = 0.1; g.eyeOpenL = g.eyeOpenR = 0.8; g.pupilY = 0.55; g.pupilSize = 1.15;
          g.mouthCurve = -0.85 + 0.08 * Math.sin(t * 14); g.mouthWidth = 0.7;
          if (t > 0.35) g.pupilX = 0;
          if (crossed(0.5)) this.spawn("tear", 120 - EYE_DX + 16, EYE_Y + 30, 0, 10, 1.4, 6, rgb(120, 200, 255));
          if (crossed(1.2)) this.spawn("tear", 120 + EYE_DX - 16, EYE_Y + 30, 0, 10, 1.2, 5, rgb(120, 200, 255));
          break;
        case "party":
          es[0] = es[1] = "star";
          g.mouthCurve = 1; g.mouthOpen = 0.65 + 0.2 * Math.abs(Math.sin(t * 10)); g.mouthWidth = 1.1; g.blush = 0.5;
          this.bounce = 7; this.hat = 1;
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t < 1.6) { this.sparkTimer = 0.05;
            this.spawn("confetti", frand(0, 240), -6, frand(-20, 20), frand(40, 90), 2.2, frand(4, 7), rainbow(Math.random())); }
          break;
        case "kiss":
          es[0] = "closed"; this.mouthStyle = "kiss"; g.blush = 0.9; g.browAsym = 0.5; g.pupilX = 0.4; g.pupilY = -0.2;
          if (crossed(0.35)) this.spawn("heart", 140, 168, 30, -50, 1.3, 11, rgb(255, 60, 110));
          break;
        case "toot":
          if (t < 0.45) {
            g.eyeScale = 1.2; g.pupilSize = 0.5; g.browH = 1; this.mouthStyle = "o"; g.mouthOpen = 0.4; g.mouthWidth = 0.4;
            if (t - dt <= 0.000001) { this.shake = 4;
              for (let i = 0; i < 4; i++) this.spawn("puff", i < 2 ? 18 : 222, 225, (i < 2 ? 1 : -1) * frand(10, 30), frand(-30, -15), 1.8, frand(9, 13), rgb(170, 220, 90)); }
          } else {
            es[0] = es[1] = "happy";
            g.blush = 1; g.mouthCurve = 0.9; g.mouthOpen = 0.35; g.mouthWidth = 1.0; g.browTilt = 0.7;
            this.sway = 3 * Math.sin(t * 8);
          }
          break;
        case "poke":
          if (this.pokeEye >= 0) es[this.pokeEye] = "closed";
          else es[0] = es[1] = t < 0.5 ? "closed" : "happy";
          if (t < 0.5) { g.mouthCurve = -0.5; g.mouthWidth = 0.6; g.browTilt = -0.8; }
          else { g.mouthCurve = 1; g.mouthOpen = 0.5 + 0.3 * Math.abs(Math.sin(t * 16)); g.blush = 0.7; this.bounce = 5; }
          break;
        case "hello":
          if (t < 0.5) { g.eyeOpenL = g.eyeOpenR = 0.05 + t * 1.8; }
          else { es[0] = es[1] = (t > 0.8 && t < 1.6) ? "happy" : "normal"; g.mouthCurve = 1; g.mouthOpen = 0.5; this.bounce = 6; }
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t > 0.5 && t < 1.5) { this.sparkTimer = 0.12;
            this.spawn("sparkle", frand(20, 220), frand(60, 220), 0, 0, 0.6, frand(6, 11), rgb(255, 255, 160)); }
          break;
        case "yawn": { const y = Math.sin(u * PI);
          this.mouthStyle = "o"; g.mouthOpen = 0.2 + 0.85 * y; g.mouthWidth = 0.5 + 0.25 * y;
          g.eyeOpenL = g.eyeOpenR = 1 - 0.85 * y; g.browH = 0.7 * y; g.pupilY = -0.2; break; }
        case "whistle":
          this.mouthStyle = "o"; g.mouthOpen = 0.25; g.mouthWidth = 0.28; g.pupilY = -0.7; g.pupilX = 0.6; g.browH = 0.5;
          this.sparkTimer -= dt;
          if (this.sparkTimer <= 0 && t < 1.8) { this.sparkTimer = 0.45;
            this.spawn("note", 150, 165, frand(20, 40), frand(-55, -40), 1.6, frand(8, 11), WHITE); }
          break;
        case "look":
          g.pupilX = (u < 0.33) ? -1 : (u < 0.66 ? 1 : 0); g.pupilY = 0; g.browH = 0.2; g.mouthCurve = 0.2;
          break;
      }
      const soundReaction = SOUND_REACTIONS.includes(R) || R === "mash";
      if (this.rt >= this.rdur && !(soundReaction && this.audio)) {
        this.react_ = null; this.eyeStyle = ["normal", "normal"]; this.mouthStyle = "normal";
        this.sway = 0; this.tremble = 0; this.lift = 0; this.hat = 0; this.nextIdle = frand(8, 18);
      }
    }

    update(dt, level, playing) {
      dt = clamp(dt, 0, 0.1); this.time += dt; this.audio = playing;
      this.talk = approach(this.talk, playing ? clamp(level * 1.8, 0, 1) : 0, 25, dt);
      const px = this.tgt.pupilX, py = this.tgt.pupilY;
      this.tgt = this.params(); this.tgt.pupilX = px; this.tgt.pupilY = py;
      this.eyeStyle = ["normal", "normal"]; this.mouthStyle = "normal";
      this.idle(dt);
      this.applyReaction(dt);
      if (this.glanceT > 0) { this.glanceT -= dt; this.tgt.pupilX = (this.glanceKey - 2) * 0.45; this.tgt.pupilY = 0.8; }
      const ms = this.mouthStyle;
      if (this.talk > 0.02 && ms !== "wavy" && ms !== "tongue" && ms !== "kiss")
        this.tgt.mouthOpen = clamp(Math.max(this.tgt.mouthOpen * 0.6, this.talk), 0, 1);
      if (ms === "tongue" || ms === "kiss") this.tgt.mouthOpen = 0;
      for (const k in this.cur) {
        const sp = k.startsWith("pupilX") || k.startsWith("pupilY") ? 22 : (k === "mouthOpen" ? 24 : (k === "blush" ? 6 : 14));
        this.cur[k] = approach(this.cur[k], this.tgt[k], sp, dt);
      }
      this.bounce = approach(this.bounce, 0, 3, dt); this.shake = approach(this.shake, 0, 5, dt);
      if (!this.react_) { this.sway = approach(this.sway, 0, 6, dt); this.tremble = 0; this.lift = approach(this.lift, 0, 8, dt); }
      this.updateParticles(dt);
    }

    // --------------------------------------------------------- drawing
    fillCircle(x, y, r, col) { const c = this.c; c.fillStyle = col; c.beginPath(); c.arc(x, y, Math.max(0, r + 0.5), 0, 2 * PI); c.fill(); }
    fillEllipse(x, y, rx, ry, col) { const c = this.c; c.fillStyle = col; c.beginPath(); c.ellipse(x, y, Math.max(0, rx + 0.5), Math.max(0, ry + 0.5), 0, 0, 2 * PI); c.fill(); }
    tri(x0, y0, x1, y1, x2, y2, col) { const c = this.c; c.fillStyle = col; c.beginPath(); c.moveTo(x0, y0); c.lineTo(x1, y1); c.lineTo(x2, y2); c.closePath(); c.fill(); }
    line(x0, y0, x1, y1, r, col) { const c = this.c; c.strokeStyle = col; c.lineWidth = r * 2; c.lineCap = "round"; c.beginPath(); c.moveTo(x0, y0); c.lineTo(x1, y1); c.stroke(); }
    rrect(x, y, w, h, r, col) { const c = this.c; c.fillStyle = col; c.beginPath(); c.roundRect(x, y, w, h, r); c.fill(); }
    font(pt) { this.c.font = `bold ${Math.round(pt * 1.96)}px "Helvetica Neue", Arial, sans-serif`; }

    arcLine(cx, cy, halfW, bulge, thick, col) {
      const c = this.c; c.strokeStyle = col; c.lineWidth = thick * 2; c.lineCap = "round"; c.lineJoin = "round";
      c.beginPath(); c.moveTo(cx - halfW, cy);
      for (let i = 1; i <= 10; i++) { const u = -1 + 2 * i / 10; c.lineTo(cx + u * halfW, cy + bulge * (1 - u * u)); }
      c.stroke();
    }
    heart(x, y, s, col) {
      this.fillCircle(x - s * 0.5, y - s * 0.25, s * 0.55, col); this.fillCircle(x + s * 0.5, y - s * 0.25, s * 0.55, col);
      this.tri(x - s * 1.03, y - s * 0.05, x + s * 1.03, y - s * 0.05, x, y + s * 1.1, col);
    }
    star(x, y, r, ang, col) {
      const c = this.c; c.fillStyle = col; c.beginPath();
      for (let i = 0; i < 10; i++) { const rr = (i & 1) ? r * 0.45 : r, a = ang + i * PI / 5 - PI / 2;
        i ? c.lineTo(x + rr * Math.cos(a), y + rr * Math.sin(a)) : c.moveTo(x + rr * Math.cos(a), y + rr * Math.sin(a)); }
      c.closePath(); c.fill();
    }
    note(x, y, s, col) {
      this.fillEllipse(x, y, s * 0.75, s * 0.55, col);
      this.line(x + s * 0.65, y, x + s * 0.65, y - s * 2.4, s * 0.14, col);
      this.line(x + s * 0.65, y - s * 2.4, x + s * 1.5, y - s * 1.7, s * 0.16, col);
    }
    puff(x, y, s, col) {
      this.fillCircle(x - s * 0.55, y + s * 0.1, s * 0.6, col); this.fillCircle(x + s * 0.55, y + s * 0.1, s * 0.6, col);
      this.fillCircle(x, y - s * 0.3, s * 0.75, col);
    }

    background() {
      const c = this.c; c.fillStyle = this.body; c.fillRect(0, 0, 240, 240);
      [[22, 26, 13], [44, 8, 6], [220, 30, 9], [16, 210, 10], [222, 214, 15], [200, 232, 6]].forEach(([x, y, r]) => this.fillCircle(x, y, r, this.bodyDark));
    }

    eye(idx, ex, ey) {
      const R = EYE_R * this.cur.eyeScale;
      let open = clamp(idx ? this.cur.eyeOpenR : this.cur.eyeOpenL, 0, 1.2);
      if (this.blinkT > 0) { const b = this.blinkT / this.blinkDur; open *= Math.abs(b * 2 - 1); }
      const st = this.eyeStyle[idx];
      if (st === "shades") return;
      if (st === "happy") return this.arcLine(ex, ey + 6, R * 0.8, -R * 0.45, 4.5, INK);
      if (st === "closed") return this.arcLine(ex, ey - 2, R * 0.8, R * 0.35, 4.5, INK);
      if (st === "heart") { const p = 1 + 0.12 * Math.sin(this.time * 12);
        this.heart(ex, ey - 4, R * 0.62 * p + 3, INK); this.heart(ex, ey - 4, R * 0.62 * p, rgb(240, 40, 80));
        this.fillCircle(ex - R * 0.32, ey - R * 0.32, R * 0.12, WHITE); return; }
      if (st === "star") { const p = 1 + 0.1 * Math.sin(this.time * 14 + idx);
        this.star(ex, ey, R * 0.95 * p, this.time * 2, INK); this.star(ex, ey, R * 0.95 * p - 5, this.time * 2, rgb(255, 220, 50)); return; }
      if (st === "spiral") {
        this.fillCircle(ex, ey, R + 3, INK); this.fillCircle(ex, ey, R, WHITE);
        const rot = this.spiralRot * (idx ? -1 : 1), c = this.c;
        c.strokeStyle = INK; c.lineWidth = 4.8; c.lineCap = "round"; c.lineJoin = "round"; c.beginPath(); c.moveTo(ex, ey);
        for (let i = 1; i <= 48; i++) { const a = i * (4.2 * PI / 48), r = (R - 5) * i / 48; c.lineTo(ex + r * Math.cos(a + rot), ey + r * Math.sin(a + rot)); }
        c.stroke(); return;
      }
      if (open < 0.08) return this.arcLine(ex, ey + 2, R * 0.8, R * 0.25, 4.5, INK);
      this.fillEllipse(ex, ey, R + 3, R + 3, INK); this.fillEllipse(ex, ey, R, R, WHITE);
      const pr = R * 0.46 * this.cur.pupilSize;
      const pX = clamp(this.cur.pupilX + (idx ? -this.cur.pupilCross : this.cur.pupilCross), -1, 1);
      const pY = clamp(this.cur.pupilY, -1, 1), maxOff = R - pr - 3;
      let dx = pX * maxOff, dy = pY * maxOff; const d = Math.hypot(dx, dy);
      if (d > maxOff) { dx *= maxOff / d; dy *= maxOff / d; }
      const px = ex + dx, py = ey + dy;
      this.fillCircle(px, py, pr, INK);
      this.fillCircle(px - pr * 0.35, py - pr * 0.38, pr * 0.32 + 1, WHITE);
      this.fillCircle(px + pr * 0.35, py + pr * 0.35, pr * 0.13 + 1, WHITE);
      if (open < 0.98) {
        const lidY = ey - R + 2 * R * (1 - clamp(open, 0, 1));
        this.c.fillStyle = this.body; this.c.fillRect(ex - R - 4, ey - R - 6, 2 * R + 9, lidY - (ey - R - 6));
        const h = clamp((lidY - ey) / (R + 3), -1, 1), hw = (R + 3) * Math.sqrt(1 - h * h);
        this.line(ex - hw, lidY, ex + hw, lidY, 2.5, INK);
      }
    }

    shades(ox, oy) {
      const y = EYE_Y + oy - 6;
      for (let i = 0; i < 2; i++) { const ex = 120 + (i ? EYE_DX : -EYE_DX) + ox;
        this.rrect(ex - 40, y - 22, 80, 46, 18, INK);
        this.line(ex - 22, y - 8, ex - 8, y - 18, 2.5, rgb(140, 140, 170));
        this.line(ex - 18, y + 4, ex + 2, y - 16, 2.0, rgb(100, 100, 130)); }
      this.line(120 - EYE_DX - 40 + ox, y - 20, 120 + EYE_DX + 40 + ox, y - 20, 3.5, INK);
    }

    brow(idx, ex, ey) {
      const sh = this.eyeStyle[idx] === "shades";
      const R = EYE_R * (sh ? 1 : this.cur.eyeScale);
      let by = ey - R - 15 - this.cur.browH * 10 - (idx ? this.cur.browAsym * 12 : 0);
      if (sh) by -= 6;
      const tilt = this.cur.browTilt * 8, sgn = idx ? 1 : -1;
      this.line(ex + sgn * 24, by + tilt, ex - sgn * 20, by - tilt, 5, INK);
    }

    mouth(mx, my) {
      const Wd = 52 * this.cur.mouthWidth, cv = this.cur.mouthCurve * 14, o = clamp(this.cur.mouthOpen, 0, 1.1), c = this.c;
      if (this.mouthStyle === "wavy") {
        const speed = this.react_ === "scared" ? 30 : 8;
        c.strokeStyle = INK; c.lineWidth = 7; c.lineCap = "round"; c.lineJoin = "round"; c.beginPath(); c.moveTo(mx - Wd, my);
        for (let i = 1; i <= 24; i++) { const u = -1 + 2 * i / 24; c.lineTo(mx + u * Wd, my + 6 * Math.sin(u * 3.2 * PI + this.time * speed)); }
        c.stroke(); return;
      }
      if (this.mouthStyle === "o") {
        const rx = 10 + 30 * this.cur.mouthWidth, ry = 6 + 28 * o;
        this.fillEllipse(mx, my + 4, rx + 3, ry + 3, INK); this.fillEllipse(mx, my + 4, rx, ry, MOUTH);
        if (ry > 12) this.fillEllipse(mx, my + 4 + ry * 0.55, rx * 0.6, ry * 0.35, TONGUE);
        return;
      }
      if (this.mouthStyle === "kiss") {
        const kx = mx + 8, ky = my + 2, p = 1 + 0.08 * Math.sin(this.time * 10);
        this.fillEllipse(kx, ky, 15 * p, 13 * p, INK);
        this.fillEllipse(kx, ky - 4, 11 * p, 6 * p, rgb(240, 60, 100)); this.fillEllipse(kx, ky + 4, 11 * p, 6 * p, rgb(240, 60, 100));
        this.line(kx - 7, ky, kx + 7, ky, 1.5, INK); return;
      }
      if (o < 0.06) this.arcLine(mx, my, Wd, cv, 3.5, INK);
      else {
        for (let pass = 0; pass < 3; pass++) {
          for (let x = Math.floor(-Wd - 3); x <= Wd + 3; x++) {
            const u = x / Wd; if (u < -1.06 || u > 1.06) continue;
            const uu = clamp(u, -1, 1), ln = my + cv * (1 - uu * uu), rnd = Math.sqrt(Math.max(0, 1 - uu * uu));
            const top = ln - o * 6 * rnd, bot = ln + o * 44 * rnd;
            if (pass === 0) { c.fillStyle = INK; c.fillRect(mx + x, top - 3, 1.2, bot - top + 6); }
            else if (pass === 1) { if (Math.abs(u) > 1) continue; c.fillStyle = MOUTH; c.fillRect(mx + x, top, 1.2, bot - top); }
            else { if (Math.abs(u) > 0.62 || o < 0.3) continue; const tu = u / 0.62;
              const tTop = bot - o * 18 * Math.sqrt(Math.max(0, 1 - tu * tu)) - 2;
              if (tTop < bot) { c.fillStyle = TONGUE; c.fillRect(mx + x, Math.max(tTop, top), 1.2, bot - Math.max(tTop, top)); } }
          }
        }
      }
      for (let s = -1; s <= 1; s += 2) {
        const fx = mx + s * Wd * 0.45, u = s * 0.45;
        const top = my + cv * (1 - u * u) - (o >= 0.06 ? o * 6 * Math.sqrt(1 - u * u) : 0);
        const len = 9 + 4 * clamp(o * 3, 0, 1);
        this.tri(fx - 6, top - 1, fx + 6, top - 1, fx, top + len, WHITE);
      }
      if (this.mouthStyle === "tongue") {
        const wig = 1 + 0.35 * this.talk + 0.08 * Math.sin(this.time * 18);
        const tx = mx + 10 + 3 * Math.sin(this.time * 9), ty = my + cv + 12 * wig;
        this.fillEllipse(tx, ty, 15, 17 * wig + 2, INK); this.fillEllipse(tx, ty, 12, 17 * wig - 1, TONGUE);
        this.line(tx, ty - 8 * wig, tx, ty + 9 * wig, 1.5, rgb(220, 70, 100));
        this.arcLine(mx, my, Wd, cv, 3.5, INK);
      }
    }

    drawHat(ox, oy) {
      if (this.hat <= 0) return;
      const bx = 120 + ox, by = 44 + oy;
      const lx = bx - 24, ly = by - 2, rx = bx + 24, ry = by + 3, tx = bx + 5, ty = by - 36;
      this.tri(lx - 3, ly + 2, rx + 3, ry + 2, tx, ty - 4, INK);
      this.tri(lx, ly, rx, ry, tx, ty, rgb(80, 160, 255));
      for (let i = 1; i <= 3; i++) { const k = i / 4;
        this.line(lx + (tx - lx) * k, ly + (ty - ly) * k, rx + (tx - rx) * k, ry + (ty - ry) * k, 2.5, i === 2 ? rgb(255, 230, 70) : rgb(255, 90, 140)); }
      this.fillCircle(tx, ty, 8, INK); this.fillCircle(tx, ty, 6, rainbow(this.time * 0.8));
    }

    particles() {
      const c = this.c;
      for (const q of this.p) {
        const a = q.life / q.maxLife, grow = 1 + (1 - a) * 0.6;
        switch (q.type) {
          case "heart": this.heart(q.x, q.y, q.size * (0.6 + 0.4 * a), q.color); break;
          case "note": this.note(q.x, q.y, q.size, q.color); break;
          case "puff": this.puff(q.x, q.y, q.size * (a > 0.25 ? 1 : a * 4), q.color); break;
          case "sparkle": { const s = q.size * Math.sin(a * PI);
            this.line(q.x - s, q.y, q.x + s, q.y, 1.5, q.color); this.line(q.x, q.y - s, q.x, q.y + s, 1.5, q.color); break; }
          case "sweat": case "tear":
            this.fillCircle(q.x, q.y, q.size, q.color); this.tri(q.x - q.size, q.y - 1, q.x + q.size, q.y - 1, q.x, q.y - q.size * 2.2, q.color); break;
          case "drop": this.fillCircle(q.x, q.y, q.size, q.color); break;
          case "confetti": c.save(); c.translate(q.x, q.y); c.rotate(q.angle * 2); c.fillStyle = q.color;
            c.fillRect(-q.size, -q.size * 0.45, q.size * 2, q.size * 0.9); c.restore(); break;
          case "orbit": { const ox = 120 + this.sway + 78 * Math.cos(q.angle), oy = 26 + 14 * Math.sin(q.angle);
            this.star(ox, oy, q.size, q.angle * 2, INK); this.star(ox, oy, q.size - 2.5, q.angle * 2, q.color); break; }
          case "bubble": {
            this.font(12); const w = c.measureText(q.text).width + 24, pop = clamp((q.maxLife - q.life) * 6, 0, 1);
            const bw = w * pop, bh = 34 * pop; if (bw < 8) break;
            this.rrect(q.x - bw / 2 - 2, q.y - bh / 2 - 2, bw + 4, bh + 4, 14, INK);
            this.rrect(q.x - bw / 2, q.y - bh / 2, bw, bh, 12, WHITE);
            this.tri(q.x - 10, q.y + bh / 2 - 2, q.x + 6, q.y + bh / 2 - 2, q.x - 4, q.y + bh / 2 + 12, WHITE);
            if (pop >= 1) { c.fillStyle = INK; c.textAlign = "center"; c.textBaseline = "middle"; c.fillText(q.text, q.x, q.y + 1); }
            break; }
          case "text": {
            this.font(24 * q.size * 0.7); c.textAlign = "center"; c.textBaseline = "middle";
            c.fillStyle = INK; c.fillText(q.text, q.x + 2, q.y + 2); c.fillStyle = q.color; c.fillText(q.text, q.x, q.y); break; }
        }
      }
    }

    render() {
      const c = this.c;
      c.save(); c.setTransform(this.cv.width / 240, 0, 0, this.cv.height / 240, 0, 0);
      this.background();
      const ox = this.sway + (this.shake > 0.3 ? Math.sin(this.time * 70) * this.shake : 0) + (this.tremble > 0 ? Math.sin(this.time * 90) * this.tremble : 0);
      const oy = -Math.abs(Math.sin(this.time * 13)) * this.bounce + this.lift;
      if (this.cur.blush > 0.25) { const b = this.cur.blush > 0.5 ? this.blushC : rgb(255, 170, 170);
        this.fillEllipse(120 - EYE_DX - 12 + ox, EYE_Y + 50 + oy, 18, 10, b); this.fillEllipse(120 + EYE_DX + 12 + ox, EYE_Y + 50 + oy, 18, 10, b); }
      for (let i = 0; i < 2; i++) this.eye(i, 120 + (i ? EYE_DX : -EYE_DX) + ox, EYE_Y + oy);
      if (this.eyeStyle[0] === "shades") this.shades(ox, oy);
      for (let i = 0; i < 2; i++) this.brow(i, 120 + (i ? EYE_DX : -EYE_DX) + ox, EYE_Y + oy);
      this.mouth(120 + ox, MOUTH_Y + oy);
      this.drawHat(ox, oy);
      this.particles();
      c.restore();
    }
  }
  Face.PALETTES = PALETTES;
  return Face;
})();
