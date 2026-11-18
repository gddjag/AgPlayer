(() => {
  'use strict';
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');

  // Offset neighboring traces in phase so they open into a ribbon and cross again.
  function drawHeroWave(context, width, height, time) {
    const phase = time * .00028;
    const narrow = width < 650;
    const count = narrow ? 15 : 23;
    const stroke = context.createLinearGradient(0, 0, width, 0);
    stroke.addColorStop(0, '#8574f2');
    stroke.addColorStop(.38, '#a68df8');
    stroke.addColorStop(.68, '#738fe3');
    stroke.addColorStop(1, '#79b5e9');
    context.strokeStyle = stroke;
    context.lineWidth = narrow ? .85 : 1.05;

    for (let line = 0; line < count; line++) {
      const offset = line / (count - 1) - .5;
      context.globalAlpha = .23 + .22 * (1 - Math.abs(2 * offset));
      context.beginPath();
      for (let x = 0; x <= width + 6; x += 6) {
        const u = x / width;
        const sweep = Math.sin(u * (narrow ? 4.3 : 7.6) + phase + 1.1 + offset * 1.65);
        const ripple = Math.sin(u * 12.4 - phase * .63 + offset * 1.9);
        const y = height * (.55 + .205 * sweep + .062 * ripple + .104 * offset);
        if (x === 0) context.moveTo(x, y);
        else context.lineTo(x, y);
      }
      context.stroke();
    }
    context.globalAlpha = 1;
  }
  // Shallow layers of frequency contours drift behind the listening-room heading.
  function drawStudioField(context, width, height, time) {
    const phase = time * .00022;
    const rows = width < 640 ? 13 : 22;
    const gradient = context.createLinearGradient(0, 0, width, 0);
    gradient.addColorStop(0, '#766ab4');
    gradient.addColorStop(.58, '#8397ef');
    gradient.addColorStop(1, '#62b8d3');
    context.strokeStyle = gradient;
    context.lineWidth = .95;
    for (let row = 0; row < rows; row++) {
      const depth = row / (rows - 1);
      context.beginPath();
      context.globalAlpha = .10 + depth * .23;
      for (let x = 0; x <= width + 7; x += 7) {
        const u = x / width;
        const ridge = u - .74 + (depth - .5) * .12;
        const envelope = Math.exp(-12 * ridge * ridge);
        const harmonics = .58 + .27 * Math.sin(u * 22 + phase - depth * 3)
          + .15 * Math.cos(u * 39 - phase * .8 - depth * 2);
        const y = height * (.27 + depth * .29 - u * .13 - envelope * harmonics * .22);
        if (x === 0) context.moveTo(x, y);
        else context.lineTo(x, y);
      }
      context.stroke();
    }
    context.globalAlpha = 1;
  }

  document.querySelectorAll('.sound-field').forEach(canvas => {
    const context = canvas.getContext('2d');
    if (!context) return;
    const drawField = canvas.classList.contains('listen-field') ? drawStudioField :
      canvas.classList.contains('hero-wave') ? drawHeroWave : null;
    let visible = false, frame = 0, last = 0;
    function draw(time) {
      const { width, height } = canvas.getBoundingClientRect();
      if (!width || !height) return;
      // Keep fine lines sharp at browser zoom, with a 16-megapixel budget for this full-screen layer.
      const dpr = Math.min(window.devicePixelRatio || 1, Math.sqrt(16777216 / (width * height)));
      const pixelWidth = Math.round(width * dpr), pixelHeight = Math.round(height * dpr);
      if (canvas.width !== pixelWidth) canvas.width = pixelWidth;
      if (canvas.height !== pixelHeight) canvas.height = pixelHeight;
      context.setTransform(pixelWidth / width, 0, 0, pixelHeight / height, 0, 0);
      context.clearRect(0, 0, width, height);
      if (drawField) {
        drawField(context, width, height, time);
        return;
      }
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
    function sync() {
      cancelAnimationFrame(frame);
      frame = 0;
      if (!visible || document.hidden) return;
      draw(reduced.matches ? 0 : performance.now());
      if (!reduced.matches) frame = requestAnimationFrame(tick);
    }
    new IntersectionObserver(entries => { visible = entries[0].isIntersecting; sync(); }).observe(canvas);
    if (window.AgWaveform?.observeSize) window.AgWaveform.observeSize(canvas, sync);
    else new ResizeObserver(sync).observe(canvas);
    document.addEventListener('visibilitychange', sync); reduced.addEventListener('change', sync);
  });
})();

