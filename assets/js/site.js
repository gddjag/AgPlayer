(() => {
  'use strict';
  const header = document.querySelector('.site-header');
  const toggle = document.querySelector('.menu-toggle');
  const nav = document.querySelector('.main-nav');
  function menu(open) {
    header.classList.toggle('menu-open', open);
    toggle.setAttribute('aria-expanded', String(open));
    toggle.dataset.i18nAria = open ? 'navigation.closeMenu' : 'navigation.openMenu';
    toggle.setAttribute('aria-label', AG.t(toggle.dataset.i18nAria));
  }
  toggle.addEventListener('click', () => menu(toggle.getAttribute('aria-expanded') !== 'true'));
  nav.addEventListener('click', event => { if (event.target.closest('a')) menu(false); });
  document.addEventListener('keydown', event => { if (event.key === 'Escape' && toggle.getAttribute('aria-expanded') === 'true') { menu(false); toggle.focus(); } });
  document.addEventListener('click', event => { if (!header.contains(event.target)) menu(false); });
  matchMedia('(min-width: 951px)').addEventListener('change', event => { if (event.matches) menu(false); });
  const sections = ['home', 'waveforms', 'features', 'tools'].map(id => document.getElementById(id)).filter(Boolean);
  let pending = false;
  function updateScroll() {
    pending = false;
    header.classList.toggle('scrolled', scrollY > 20);
    if (!sections.length) return;
    let current = sections[0].id;
    for (const section of sections) if (section.getBoundingClientRect().top <= innerHeight * .35) current = section.id;
    document.querySelectorAll('[data-nav]').forEach(link => {
      if (link.dataset.nav === current) link.setAttribute('aria-current', 'location'); else link.removeAttribute('aria-current');
    });
  }
  addEventListener('scroll', () => { if (!pending) { pending = true; requestAnimationFrame(updateScroll); } }, { passive: true });
  updateScroll();
  // Native anchors retain browser history, direct links, and reduced-motion behavior.
  window.agTabs = (buttons, callback) => {
    buttons.forEach((button, index) => {
      button.addEventListener('click', () => {
        buttons.forEach(item => { item.setAttribute('aria-selected', String(item === button)); item.tabIndex = item === button ? 0 : -1; });
        callback(button);
      });
      button.addEventListener('keydown', event => {
        let next;
        if (event.key === 'ArrowRight' || event.key === 'ArrowDown') next = (index + 1) % buttons.length;
        if (event.key === 'ArrowLeft' || event.key === 'ArrowUp') next = (index + buttons.length - 1) % buttons.length;
        if (event.key === 'Home') next = 0;
        if (event.key === 'End') next = buttons.length - 1;
        if (next !== undefined) { event.preventDefault(); buttons[next].focus(); buttons[next].click(); }
      });
    });
  };
  const shot = document.querySelector('#theme-display > img');
  if (shot) agTabs([...document.querySelectorAll('[data-theme]')], button => {
    const theme = button.dataset.theme;
    shot.src = `assets/images/player-${theme}.png`;
    shot.className = `product-shot shot-${theme}`;
    shot.dataset.i18nAlt = `themes.${theme}.imageAlt`;
    shot.alt = AG.t(shot.dataset.i18nAlt);
    document.getElementById('theme-display').setAttribute('aria-labelledby', button.id);
  });
})();
