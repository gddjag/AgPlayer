(() => {
  'use strict';
  const root = document.getElementById('audio-demo');
  if (!root) return;
  const localNote = root.nextElementSibling;
  const formatNote = localNote?.nextElementSibling;
  if (localNote?.classList.contains('local-note') && formatNote?.classList.contains('format-note')) {
    const footer = document.createElement('div');
    const notes = document.createElement('div');
    const stageLink = document.createElement('a');
    footer.className = 'audio-demo-footer';
    notes.className = 'audio-demo-notes';
    stageLink.className = 'immersive-stage-link';
    stageLink.href = 'assets/Beach_Voxel_Festival.html';
    stageLink.target = '_blank';
    stageLink.rel = 'noopener';
    stageLink.innerHTML = '<svg class="icon" aria-hidden="true"><use href="#ag-icon-wave"></use></svg><span data-i18n="audioDemo.immersiveStage"></span><span class="stage-arrow" aria-hidden="true">↗</span>';
    stageLink.querySelector('[data-i18n]').textContent = AG.t('audioDemo.immersiveStage');
    localNote.before(footer);
    notes.append(localNote, formatNote);
    footer.append(notes, stageLink);
  }
  const input = document.getElementById('audio-file'), audio = document.getElementById('local-audio'), play = document.getElementById('audio-play'), seek = document.getElementById('audio-seek'), canvas = document.getElementById('audio-canvas'), filename = document.getElementById('audio-filename'), clock = document.getElementById('audio-time'), status = document.getElementById('audio-status'), placeholder = document.getElementById('audio-placeholder');
  let context, analyser, source, url, generation = 0, data, mode = 'solid', state = 'empty', name = '', frame = 0, lastFrame = 0, inView = true;
  const spectrum = new AgWaveform.Spectrum(), samples = new Float32Array(512);
  const visual = canvas.parentElement;
  const orbit = document.createElement('div');
  orbit.className = 'play-orbit'; play.before(orbit); orbit.append(play);
  const capsule = document.createElement('span'), cursor = document.createElement('span');
  capsule.className = 'wave-time-capsule'; cursor.className = 'wave-hover-line';
  capsule.setAttribute('aria-hidden', 'true'); cursor.setAttribute('aria-hidden', 'true');
  visual.append(seek, cursor, capsule);
  let hoverRatio = null;
  function showPosition() {
    const duration = Number.isFinite(audio.duration) ? audio.duration : 0;
    capsule.hidden = cursor.hidden = !data || !duration;
    if (!data || !duration) return;
    const ratio = hoverRatio ?? Math.min(1, audio.currentTime / duration);
    capsule.textContent = time(ratio * duration);
    const width = visual.clientWidth;
    capsule.style.left = `${Math.max(29, Math.min(width - 29, ratio * width))}px`;
    cursor.style.left = `${Math.min(width - 1, ratio * width)}px`;
    cursor.classList.toggle('is-hovering', hoverRatio !== null);
  }
  const time = seconds => { const n = Math.max(0, Math.floor(Number.isFinite(seconds) ? seconds : 0)); return `${String(Math.floor(n / 60)).padStart(2, '0')}:${String(n % 60).padStart(2, '0')}`; };
  function text() {
    const active = state === 'playing';
    filename.textContent = name || AG.t('audioDemo.empty.label');
    filename.title = name;
    filename.removeAttribute('data-i18n');
    status.textContent = state === 'empty' ? '' : AG.t(`audioDemo.${state}.${state === 'error' ? 'unsupported' : 'label'}`);
    placeholder.textContent = AG.t(`audioDemo.${state === 'loading' ? 'loading' : state === 'error' ? 'error' : 'empty'}.${state === 'error' ? 'unsupported' : 'description'}`);
    placeholder.removeAttribute('data-i18n');
    placeholder.hidden = !!data;
    play.setAttribute('aria-label', AG.t(`audioDemo.${active ? 'pause' : state === 'ended' ? 'replay' : 'play'}`));
    play.querySelector('use').setAttribute('href', `#ag-icon-${active ? 'pause' : 'play'}`);
    play.disabled = !data; seek.disabled = !data;
    root.setAttribute('aria-busy', String(state === 'loading'));
    root.dataset.state = state;
    play.classList.toggle('is-playing', active);
    const pickerText = document.querySelector('.file-picker [data-i18n]');
    pickerText.dataset.i18n = name ? 'audioDemo.replaceFile' : 'audioDemo.chooseFile'; pickerText.textContent = AG.t(pickerText.dataset.i18n);
    updateTime();
  }
  function setState(value) { state = value; text(); }
  function updateTime() {
    const duration = Number.isFinite(audio.duration) ? audio.duration : 0;
    clock.textContent = `${time(audio.currentTime)} / ${time(duration)}`;
    seek.value = duration ? Math.round(audio.currentTime / duration * 1000) : 0;
    seek.setAttribute('aria-valuetext', AG.t('audioDemo.seekValue', { current: time(audio.currentTime), duration: time(duration) }));
    showPosition();
  }
  function draw() {
    if (!data) { canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height); return; }
    if (mode === 'spectrum') spectrum.draw(canvas);
    else AgWaveform.wave(canvas, data, mode, audio.duration ? Math.min(1, audio.currentTime / audio.duration) : 0);
  }
  function tick(now) {
    frame = 0;
    if (audio.paused || document.hidden || !inView || !data) return;
    if (now - lastFrame >= 32) {
      if (mode === 'spectrum') { analyser.getFloatTimeDomainData(samples); spectrum.update(samples, Math.min(.1, Math.max(.001, (now - lastFrame) / 1000))); }
      lastFrame = now; draw(); updateTime();
    }
    frame = requestAnimationFrame(tick);
  }
  function sync() { cancelAnimationFrame(frame); frame = 0; root.classList.toggle('motion-paused', document.hidden || !inView); draw(); if (!audio.paused && data && !document.hidden && inView) frame = requestAnimationFrame(tick); }
  function initAudio() {
    if (!context) {
      const AudioContext = window.AudioContext || window.webkitAudioContext;
      context = new AudioContext();
      analyser = context.createAnalyser(); analyser.fftSize = 512; analyser.smoothingTimeConstant = 0;
      source = context.createMediaElementSource(audio); source.connect(analyser); analyser.connect(context.destination);
    }
  }
  input.addEventListener('change', async () => {
    const file = input.files[0]; input.value = ''; if (!file) return;
    const task = ++generation;
    audio.pause(); audio.removeAttribute('src'); audio.load();
    if (url) { URL.revokeObjectURL(url); url = undefined; }
    data = undefined; hoverRatio = null; name = file.name; spectrum.engine.fill(0); spectrum.shown.fill(0); spectrum.held.fill(0); setState('loading'); sync();
    try {
      initAudio();
      url = URL.createObjectURL(file); audio.src = url; audio.load();
      const buffer = await context.decodeAudioData(await file.arrayBuffer());
      if (task !== generation) return;
      const analyzed = await AgWaveform.analyze(buffer, () => task !== generation, true);
      if (task !== generation) return;
      data = analyzed; setState('ready'); draw();
      // Make the default waveform usable before calculating the three frequency bands.
      await new Promise(resolve => setTimeout(resolve, 0));
      if (task !== generation) return;
      const detailed = await AgWaveform.analyze(buffer, () => task !== generation);
      if (task !== generation) return;
      data = detailed; draw();
    } catch (error) { if (task === generation && error.name !== 'AbortError') { data = undefined; setState('error'); draw(); } }
  });
  play.addEventListener('click', async () => {
    if (!data) return;
    if (!audio.paused) { audio.pause(); return; }
    const task = generation;
    try {
      // Safari requires play() during the original user gesture, before any await.
      if (audio.ended) audio.currentTime = 0;
      const resume = context.resume();
      const playback = audio.play();
      await Promise.all([resume, playback]);
    }
    catch (_) { if (task === generation) status.textContent = AG.t('audioDemo.error.playback'); }
  });
  audio.addEventListener('play', () => { setState('playing'); sync(); });
  audio.addEventListener('pause', () => { if (data && !audio.ended) setState('paused'); sync(); });
  audio.addEventListener('ended', () => { setState('ended'); sync(); });
  audio.addEventListener('error', () => { if (!audio.getAttribute('src')) return; data = undefined; setState('error'); sync(); });
  audio.addEventListener('timeupdate', () => { updateTime(); if (audio.paused) draw(); });
  audio.addEventListener('loadedmetadata', updateTime);
  seek.addEventListener('input', () => { if (data && Number.isFinite(audio.duration)) { audio.currentTime = audio.duration * Number(seek.value) / 1000; updateTime(); draw(); } });
  function pointerRatio(event) { const rect = visual.getBoundingClientRect(); return Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width)); }
  visual.addEventListener('pointermove', event => { if (event.pointerType !== 'touch') { hoverRatio = pointerRatio(event); showPosition(); } });
  visual.addEventListener('pointerleave', () => { hoverRatio = null; showPosition(); });
  visual.addEventListener('click', event => {
    if (event.target === seek || !data || !Number.isFinite(audio.duration)) return;
    audio.currentTime = pointerRatio(event) * audio.duration;
    hoverRatio = null; updateTime(); draw();
  });
  seek.addEventListener('focus', () => { hoverRatio = null; showPosition(); });
  agTabs([...document.querySelectorAll('[data-mode]')], button => { mode = button.dataset.mode; document.getElementById('audio-panel').setAttribute('aria-labelledby', button.id); draw(); });
  new ResizeObserver(() => { draw(); showPosition(); }).observe(canvas);
  new IntersectionObserver(entries => { inView = entries[0].isIntersecting; sync(); }).observe(root);
  document.addEventListener('visibilitychange', sync);
  document.addEventListener('ag:languagechange', text);
  addEventListener('pagehide', event => { if (!event.persisted) { ++generation; audio.pause(); if (url) URL.revokeObjectURL(url); if (context) context.close(); } });
  text();
})();

