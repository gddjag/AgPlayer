(() => {
  'use strict';
  let saved;
  try { saved = localStorage.getItem('ag-language'); } catch (_) { /* Storage can be disabled. */ }
  let language = ['zh', 'en'].includes(saved) ? saved : ((navigator.languages?.[0] || navigator.language || 'en').toLowerCase().startsWith('zh') ? 'zh' : 'en');
  const t = (key, values = {}) => {
    let value = key.split('.').reduce((item, part) => item?.[part], window.AG_COPY[language]);
    if (typeof value !== 'string') return key;
    return value.replace(/\{(\w+)\}/g, (_, name) => values[name] ?? '');
  };
  function apply() {
    document.documentElement.lang = language === 'zh' ? 'zh-CN' : 'en';
    for (const [attribute, target] of [['data-i18n', null], ['data-i18n-alt', 'alt'], ['data-i18n-aria', 'aria-label']]) {
      document.querySelectorAll(`[${attribute}]`).forEach(element => {
        const value = t(element.getAttribute(attribute));
        if (target) element.setAttribute(target, value); else element.textContent = value;
      });
    }
    document.querySelectorAll('[data-lang]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.lang === language)));
    document.title = "AgPlayer - 免费·轻量·波形音频播放器";
    document.querySelector('meta[name=description]')?.setAttribute('content', t('meta.siteDescription'));
    document.dispatchEvent(new Event('ag:languagechange'));
  }
  window.AG = { t, get language() { return language; }, setLanguage(value) {
    if (!['zh', 'en'].includes(value)) return;
    language = value;
    try { localStorage.setItem('ag-language', value); } catch (_) { /* Current-page switching still works. */ }
    apply();
  }};
  document.querySelectorAll('[data-lang]').forEach(button => button.addEventListener('click', () => window.AG.setLanguage(button.dataset.lang)));
  apply();
})();
