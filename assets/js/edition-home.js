(() => {
  'use strict';
  const download = document.getElementById('download');
  const platforms = [...download.querySelectorAll('[data-platform-choice]')];
  const selectPlatform = value => {
    download.dataset.platform = value;
    platforms.forEach(button => button.setAttribute('aria-pressed', String(button.dataset.platformChoice === value)));
  };
  const device = navigator.userAgentData?.platform || navigator.userAgent;
  const preferred = /harmony|openharmony/i.test(device) ? 'harmonyos' : /android/i.test(device) ? 'android'
    : /mac/i.test(device) && navigator.maxTouchPoints <= 1 ? 'macos' : 'windows';
  if (platforms.length) selectPlatform(preferred);
  platforms.forEach(button => button.addEventListener('click', () => selectPlatform(button.dataset.platformChoice)));

  function reveal(hash) {
    const target = document.getElementById(hash.slice(1));
    if (!target) return;
    const fold = target.closest('details.onepage-fold');
    if (fold && !fold.open) fold.open = true;
    const platform = target.closest('.platform');
    if (platform && platforms.length) selectPlatform(platform.id);
  }
  document.addEventListener('click', event => {
    const anchor = event.target.closest('a[href^="#"]');
    if (anchor) reveal(anchor.hash);
  });
  const onAnchor = () => {
    reveal(location.hash);
    // A direct URL can point inside a panel that was folded during initial layout.
    if (location.hash) document.getElementById(location.hash.slice(1))?.scrollIntoView();
  };
  window.addEventListener('hashchange', onAnchor);
  if (location.hash) requestAnimationFrame(onAnchor);
  const motion = matchMedia('(prefers-reduced-motion: reduce)');
  const recording = document.getElementById('recording-dialog');
  const video = document.getElementById('desktop-recording');
  const chapters = [...recording.querySelectorAll('[data-start]')];
  let nextStart = 0;

  function seekToChapter() {
    if (nextStart === null || video.readyState < 1) return;
    video.currentTime = nextStart;
    nextStart = null;
  }
  function playFrom(seconds) {
    nextStart = seconds;
    seekToChapter();
    video.play().catch(() => {});
  }
  video.addEventListener('loadedmetadata', seekToChapter);
  video.addEventListener('timeupdate', () => {
    const active = chapters.filter(button => Number(button.dataset.start) <= video.currentTime).pop();
    chapters.forEach(button => {
      if (button === active) button.setAttribute('aria-current', 'true');
      else button.removeAttribute('aria-current');
    });
  });
  chapters.forEach(button => button.addEventListener('click', () => playFrom(Number(button.dataset.start))));
  document.querySelector('[data-watch]').addEventListener('click', event => {
    if (typeof recording.showModal !== 'function' || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    recording.showModal();
    playFrom(0);
  });
  recording.querySelector('[data-close]').addEventListener('click', () => recording.close());
  recording.addEventListener('click', event => { if (event.target === recording) recording.close(); });
  recording.addEventListener('close', () => video.pause());

  const panel = document.querySelector('#feature-screen');
  const image = panel.querySelector('img');
  const photoLink = panel.querySelector('.shot-open');
  const featureCopy = panel.querySelectorAll('[data-feature-copy]');
  let screenshotRequest = 0;
  window.agTabs([...document.querySelectorAll('.feature-tab')], tab => {
    const prefix = 'featureGuide.' + tab.dataset.feature + '.';
    if (image.getAttribute('src') !== tab.dataset.src) {
      const request = ++screenshotRequest;
      image.style.visibility = 'hidden';
      image.loading = 'eager';
      image.src = tab.dataset.src;
      image.decode().catch(() => {}).then(() => {
        if (request === screenshotRequest) image.style.visibility = '';
      });
    }
    photoLink.href = tab.dataset.src;
    image.dataset.i18nAlt = prefix + 'alt';
    image.alt = AG.t(image.dataset.i18nAlt);
    featureCopy.forEach(el => {
      el.dataset.i18n = prefix + el.dataset.featureCopy;
      el.textContent = AG.t(el.dataset.i18n);
    });
    panel.setAttribute('aria-labelledby', tab.id);
  });

  const entrances = new IntersectionObserver(entries => {
    entries.forEach(entry => {
      if (!entry.isIntersecting) return;
      entrances.unobserve(entry.target);
      if (!motion.matches) entry.target.animate(
        [{opacity: .65, transform: 'translateY(15px)'}, {opacity: 1, transform: 'none'}],
        {duration: 540, easing: 'cubic-bezier(.2,.7,.25,1)'}
      );
    });
  });
  document.querySelectorAll('.feature-heading, .tool-chapter h2, .listen-heading, #faq h2').forEach(el => entrances.observe(el));

  const dialog = document.querySelector('.screen-dialog');
  if (typeof dialog.showModal !== 'function') return;
  const enlarged = dialog.querySelector('img');
  let activeImage;
  function syncDialog() {
    if (activeImage) enlarged.alt = activeImage.alt || activeImage.getAttribute('aria-label') || activeImage.textContent.trim();
  }
  for (const link of document.querySelectorAll('.shot-open, .tool-shot-link')) {
    link.addEventListener('click', event => {
      if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
      event.preventDefault();
      activeImage = link.querySelector('img') || link.closest('[data-deck]')?.querySelector('.deck-card[data-step="0"] :is(img, video)') || link;
      enlarged.src = link.href;
      syncDialog();
      dialog.showModal();
    });
  }
  dialog.querySelector('button').addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', event => { if (event.target === dialog) dialog.close(); });
  document.addEventListener('ag:languagechange', syncDialog);
})();
