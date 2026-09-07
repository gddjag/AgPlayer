(() => {
  'use strict';
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  document.querySelectorAll('.sound-field').forEach(canvas => {
    const context = canvas.getContext('2d');
    let visible = false, frame = 0, last = 0;
    function draw(time) {
      const { width, height } = canvas.getBoundingClientRect();
      if (!width || !height) return;
      const dpr = Math.min(devicePixelRatio || 1, 1.5);
      if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) { canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr); }
      context.setTransform(dpr, 0, 0, dpr, 0, 0);
      context.clearRect(0, 0, width, height);
      const glowX = width * (.56 + Math.sin(time * .00007) * .05);
      const glowY = height * (.62 + Math.cos(time * .00006) * .04);
      const gradient = context.createRadialGradient(glowX, glowY, 0, glowX, glowY, width * .65);
      gradient.addColorStop(0, 'rgba(120,61,190,.19)'); gradient.addColorStop(1, 'rgba(38,15,65,0)');
      context.fillStyle = gradient; context.fillRect(0, 0, width, height);
      const hero = canvas.closest('.hero');
      for (let line = 0; line < 18; line++) {
        context.beginPath();
        for (let x = 0; x <= width + 8; x += 8) {
          const u = x / width;
          const envelope = Math.pow(Math.sin(Math.PI * u), 2);
          const y = height * .5 + Math.sin(u * 7.5 + time * .00012 + line * .12) * height * (hero ? .24 : .32) * envelope + Math.cos(u * 13 - time * .00007 + line * .1) * height * .045 + (line - 7) * 5;
          if (x === 0) context.moveTo(x, y); else context.lineTo(x, y);
        }
        context.strokeStyle = `rgba(${line > 9 ? '173,111,233' : '128,76,210'},${(hero ? .085 : .09) + line * .004})`;
        context.lineWidth = hero ? 1 : 1.25; context.stroke();
      }
    }
    function tick(time) { frame = 0; if (!visible || document.hidden || reduced.matches) return; if (time - last >= 50) { last = time; draw(time); } frame = requestAnimationFrame(tick); }
    function sync() { cancelAnimationFrame(frame); frame = 0; draw(performance.now()); if (visible && !document.hidden && !reduced.matches) frame = requestAnimationFrame(tick); }
    new IntersectionObserver(entries => { visible = entries[0].isIntersecting; sync(); }).observe(canvas);
    new ResizeObserver(sync).observe(canvas);
    document.addEventListener('visibilitychange', sync); reduced.addEventListener('change', sync);
  });
})();
