(() => {
  'use strict';

  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const wrap = (index, length) => (index + length) % length;

  // Both showcases share one transition at a time, only while they are on screen.
  function install(root) {
    const tabs = [...root.querySelectorAll('[data-deck-choice]')];
    const cards = [...root.querySelectorAll('[data-deck-slide]')];
    const stage = root.querySelector('.deck-stage');
    const pause = root.querySelector('[data-deck-pause]');
    const zoom = root.querySelector('.deck-zoom');
    const heading = root.querySelector('[data-deck-title]');
    const body = root.querySelector('[data-deck-description]');
    const videos = [...root.querySelectorAll('video')];
    let current = tabs.findIndex(tab => tab.dataset.deckChoice === root.dataset.active);
    let paused = reduced.matches;
    let visible = false;
    let timer;

    function sync() {
      clearTimeout(timer);
      const available = visible && !document.hidden && !document.querySelector('dialog[open]');
      const focused = document.activeElement;
      const reading = root.contains(focused) && focused !== pause && focused.matches(':focus-visible');
      const rotating = available && !paused && !reading;
      if (rotating) timer = setTimeout(() => select(current + 1), 6000);
      videos.forEach(video => {
        if (available && !paused && cards[current].contains(video)) video.play().catch(() => {});
        else video.pause();
      });
      pause.dataset.i18nAria = paused ? 'showcase.resume' : 'showcase.pause';
      pause.setAttribute('aria-label', AG.t(pause.dataset.i18nAria));
      pause.setAttribute('aria-pressed', String(paused));
      pause.querySelector('use').setAttribute('href', paused ? '#ag-icon-play' : '#ag-icon-pause');
    }

    function text(node, key) {
      node.dataset.i18n = key;
      node.textContent = AG.t(key);
    }

    function select(index) {
      current = wrap(index, tabs.length);
      const selected = tabs[current];
      root.dataset.active = selected.dataset.deckChoice;
      tabs.forEach((tab, i) => {
        tab.setAttribute('aria-selected', String(i === current));
        tab.tabIndex = i === current ? 0 : -1;
        let step = wrap(i - current, tabs.length);
        if (step > tabs.length / 2) step -= tabs.length;
        cards[i].dataset.step = step;
        cards[i].setAttribute('aria-hidden', String(i !== current));
      });
      text(heading, selected.dataset.title);
      text(body, selected.dataset.description);
      text(zoom.querySelector('span'), selected.dataset.title);
      zoom.href = cards[current].dataset.picture;
      sync();
    }

    window.agTabs(tabs, tab => select(tabs.indexOf(tab)));
    root.querySelector('[data-deck-previous]').addEventListener('click', () => select(current - 1));
    root.querySelector('[data-deck-next]').addEventListener('click', () => select(current + 1));
    pause.addEventListener('click', () => { paused = !paused; sync(); });

    let touch = null;
    let lastSwipe = -Infinity;
    stage.addEventListener('pointerdown', event => {
      if (event.isPrimary && event.pointerType !== 'mouse') touch = { x: event.clientX, y: event.clientY };
    });
    stage.addEventListener('pointercancel', () => { touch = null; });
    stage.addEventListener('pointerup', event => {
      if (!touch || !event.isPrimary) return;
      const dx = event.clientX - touch.x, dy = event.clientY - touch.y;
      touch = null;
      if (Math.abs(dx) < 32 || Math.abs(dx) < Math.abs(dy) * 1.3) return;
      lastSwipe = performance.now();
      select(current + (dx < 0 ? 1 : -1));
    });
    stage.addEventListener('click', event => {
      if (performance.now() - lastSwipe < 350) return;
      const card = event.target.closest('[data-deck-slide]');
      if (card === cards[current]) zoom.click();
      else if (card) select(cards.indexOf(card));
    });
    new IntersectionObserver(([entry]) => {
      visible = entry.intersectionRatio >= .15;
      sync();
    }, { threshold: [0, .15] }).observe(stage);
    for (const dialog of document.querySelectorAll('dialog')) {
      new MutationObserver(sync).observe(dialog, { attributeFilter: ['open'] });
    }
    root.addEventListener('focusin', sync);
    root.addEventListener('focusout', () => queueMicrotask(sync));
    document.addEventListener('visibilitychange', sync);
    document.addEventListener('ag:languagechange', sync);
    reduced.addEventListener('change', () => { paused = reduced.matches; sync(); });
    addEventListener('pagehide', () => { clearTimeout(timer); videos.forEach(video => video.pause()); });
    addEventListener('pageshow', sync);
    select(current);
  }
  document.querySelectorAll('[data-deck]').forEach(install);
})();
