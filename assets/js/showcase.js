(() => {
  'use strict';
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const fine = matchMedia('(hover:hover) and (pointer:fine)');
  // One scheduled transition while visible; no background rendering loop.
  function automatic(root, name, advance) {
    const button = document.querySelector(`[data-pause="${name}"]`);
    let visible = false, hovering = false, focused = false, paused = false, timer;
    function sync() {
      clearTimeout(timer);
      button.setAttribute('aria-label', AG.t(paused ? 'showcase.resume' : 'showcase.pause'));
      button.setAttribute('aria-pressed', String(paused));
      button.querySelector('use').setAttribute('href', `#ag-icon-${paused ? 'play' : 'pause'}`);
      if (visible && !hovering && !focused && !paused && !document.hidden && !reduced.matches) timer = setTimeout(() => { advance(); sync(); }, 5200);
    }
    button.addEventListener('click', () => { paused = !paused; sync(); });
    root.addEventListener('pointerenter', event => { if (event.pointerType === 'mouse') { hovering = true; sync(); } });
    root.addEventListener('pointerleave', () => { hovering = false; sync(); });
    // Transformed descendants can retain a pointer-enter boundary in Chromium.
    // Reconcile with the actual hit target when the mouse leaves the showcase.
    document.addEventListener('pointermove', event => {
      if (event.pointerType !== 'mouse') return;
      const inside = root.contains(event.target);
      if (inside !== hovering) { hovering = inside; sync(); }
    }, { passive: true });
    root.addEventListener('focusin', event => { focused = event.target !== button; sync(); });
    root.addEventListener('focusout', event => { focused = event.relatedTarget !== button && root.contains(event.relatedTarget); sync(); });
    new IntersectionObserver(entries => { visible = entries[0].intersectionRatio >= .2; sync(); }, { threshold: .2 }).observe(root);
    document.addEventListener('visibilitychange', sync);
    document.addEventListener('ag:languagechange', sync);
    reduced.addEventListener('change', sync);
    addEventListener('pagehide', () => clearTimeout(timer));
    addEventListener('pageshow', sync);
    return sync;
  }
  const themes = [...document.querySelectorAll('button[data-theme]')];
  const slides = [...document.querySelectorAll('[data-screen]')];
  const panel = document.getElementById('theme-display');
  let activeTheme = 2;
  function setInfo(element, key) {
    element.querySelector('h2,h3').dataset.i18n = `${key}.${key.startsWith('themes') ? 'name' : 'title'}`;
    element.querySelector('p').dataset.i18n = `${key}.description`;
    element.querySelectorAll('[data-i18n]').forEach(node => { node.textContent = AG.t(node.dataset.i18n); });
    if (!reduced.matches) element.animate([{ opacity: .2, transform: 'translateY(8px)' }, { opacity: 1, transform: 'translateY(0)' }], { duration: 380, easing: 'ease-out' });
  }
  function theme(index) {
    activeTheme = (index + themes.length) % themes.length;
    themes.forEach((button, i) => { button.setAttribute('aria-selected', String(i === activeTheme)); button.tabIndex = i === activeTheme ? 0 : -1; });
    slides.forEach((slide, i) => {
      let offset = (i - activeTheme + slides.length) % slides.length;
      if (offset > 1) offset -= slides.length;
      slide.style.setProperty('--card-x', `${offset * 35}%`);
      slide.style.setProperty('--card-turn', `${offset * -24}deg`);
      slide.style.setProperty('--card-depth', `${offset ? -75 : 15}px`);
      slide.style.setProperty('--card-scale', offset ? '.82' : '1');
      slide.style.zIndex = offset ? '1' : '3';
      slide.classList.toggle('is-active', i === activeTheme);
      slide.setAttribute('aria-hidden', String(i !== activeTheme));
    });
    panel.setAttribute('aria-labelledby', themes[activeTheme].id);
    panel.dataset.theme = themes[activeTheme].dataset.theme;
    setInfo(document.querySelector('.theme-info'), `themes.${themes[activeTheme].dataset.theme}`);
  }
  agTabs(themes, button => theme(themes.indexOf(button)));
  themes.forEach((button, index) => button.addEventListener('pointerenter', () => { if (fine.matches && index !== activeTheme) theme(index); }));
  let themeHoverLock = 0;
  panel.addEventListener('pointermove', event => {
    if (!fine.matches || performance.now() < themeHoverLock) return;
    const slide = event.target.closest('[data-screen]');
    const index = slides.indexOf(slide);
    if (index >= 0 && index !== activeTheme) { themeHoverLock = performance.now() + 950; theme(index); }
  });
  panel.addEventListener('click', event => {
    const index = slides.indexOf(event.target.closest('[data-screen]'));
    if (index >= 0 && index !== activeTheme) theme(index);
  });
  theme(activeTheme);
  automatic(document.querySelector('.hero-showcase'), 'themes', () => theme(activeTheme + 1));

  const cards = [...document.querySelectorAll('.orbit-card')];
  const nav = [...document.querySelectorAll('[data-tool-nav]')];
  const orbit = document.querySelector('.tool-orbit');
  let activeTool = 0, hoverLockUntil = 0;
  function positionCards() {
    const compact = innerWidth <= 600;
    const radius = Math.min(orbit.clientWidth * (compact ? .42 : .34), 450);
    cards.forEach((card, i) => {
      let step = (i - activeTool + cards.length) % cards.length;
      if (step > 3) step -= cards.length;
      const angle = step * Math.PI / 3;
      const depth = Math.cos(angle);
      card.style.setProperty('--x', `${Math.sin(angle) * radius}px`);
      card.style.setProperty('--y', `${(1 - depth) * (compact ? -14 : -35)}px`);
      card.style.setProperty('--z', `${depth * (compact ? 65 : 135)}px`);
      card.style.setProperty('--turn', `${-Math.sin(angle) * 32}deg`);
      card.style.setProperty('--scale', String(.72 + (depth + 1) * .14));
      card.style.zIndex = String(Math.round((depth + 1) * 10));
      card.style.setProperty('--dim', String(.38 + (depth + 1) * .31));
      card.classList.toggle('is-active', i === activeTool);
      card.setAttribute('aria-pressed', String(i === activeTool));
      // Rear cards remain mouse-selectable; the six explicit controls own keyboard navigation.
      card.tabIndex = i === activeTool ? 0 : -1;
    });
  }
  function tool(index) {
    activeTool = (index + cards.length) % cards.length;
    hoverLockUntil = performance.now() + 800;
    positionCards();
    nav.forEach((button, i) => button.setAttribute('aria-pressed', String(i === activeTool)));
    orbit.dataset.active = cards[activeTool].dataset.tool;
    setInfo(document.querySelector('.tool-info'), `tools.${cards[activeTool].dataset.tool}`);
  }
  cards.forEach((card, i) => {
    card.addEventListener('click', () => tool(i));
    card.addEventListener('pointermove', () => { if (fine.matches && i !== activeTool && performance.now() > hoverLockUntil) tool(i); });
    card.addEventListener('keydown', event => { if (event.key === 'ArrowRight' || event.key === 'ArrowLeft') { event.preventDefault(); tool(activeTool + (event.key === 'ArrowRight' ? 1 : -1)); cards[activeTool].focus(); } });
  });
  nav.forEach((button, i) => { button.addEventListener('click', () => tool(i)); button.addEventListener('pointerenter', () => { if (fine.matches && i !== activeTool) tool(i); }); });
  document.querySelectorAll('[data-tool-step]').forEach(button => button.addEventListener('click', () => tool(activeTool + Number(button.dataset.toolStep))));
  new ResizeObserver(positionCards).observe(orbit);
  tool(0);
  automatic(document.querySelector('.tools-showcase'), 'tools', () => tool(activeTool + 1));

  const marks = { waveforms: 'wave', features: 'sliders', tools: 'scissors', mini: 'window', privacy: 'lock', rediscover: 'folder' };
  for (const [id, symbol] of Object.entries(marks)) {
    const heading = document.querySelector(`#${id} h2`);
    if (heading) { const mark = document.createElement('span'); mark.className = 'chapter-mark'; mark.setAttribute('aria-hidden', 'true'); mark.innerHTML = `<svg class="icon"><use href="#ag-icon-${symbol}"></use></svg>`; heading.before(mark); }
  }
  const reveals = document.querySelectorAll('.section-heading, .feature-list li, .mini-layout, .privacy-layout, .closing-content');
  const reveal = new IntersectionObserver(entries => entries.forEach(entry => { if (entry.isIntersecting) { entry.target.classList.add('is-seen'); reveal.unobserve(entry.target); } }), { threshold: .08 });
  reveals.forEach(element => { element.classList.add('showcase-reveal'); reveal.observe(element); });
})();
