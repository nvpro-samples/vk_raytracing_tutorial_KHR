// Reload the page whenever the user toggles MkDocs Material's palette.
// mkdocs-mermaid2-plugin initializes Mermaid at page load with either the
// 'dark' or 'default' theme based on the palette index (see mkdocs.yml).
// Re-initializing Mermaid at runtime is not supported, so a full reload is
// the simplest way to redraw all diagrams with the new theme.
//
// Source: https://mkdocs-mermaid2.readthedocs.io/en/latest/tips/#material-theme-switching-on-the-fly-between-light-and-dark-mode

(function () {
  var switchers = [
    document.getElementById("__palette_1"),
    document.getElementById("__palette_2"),
  ];
  switchers.forEach(function (el) {
    if (el) {
      el.addEventListener("change", function () {
        location.reload();
      });
    }
  });
})();
