// graph.js — Plotly.js visualization frontend for kdbZPH
// Iteration 10: plot q expressions as interactive charts
(function () {
  'use strict';

  var exprEl = document.getElementById('graph-expr');
  var chartTypeEl = document.getElementById('chart-type');
  var plotBtn = document.getElementById('plot-btn');
  var chartEl = document.getElementById('plotly-chart');

  if (!exprEl || !plotBtn || !chartEl) return;

  function showError(msg) {
    chartEl.innerHTML = '<p class="graph-error">' + msg + '</p>';
  }

  function doPlot() {
    var expr = exprEl.value.trim();
    if (!expr) return;

    plotBtn.disabled = true;

    fetch('/', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'plot', expr: expr })
    })
      .then(function (resp) { return resp.json(); })
      .then(function (data) {
        if (data.error) { showError('Error: ' + data.error); return; }
        var chartType = chartTypeEl ? chartTypeEl.value : 'line';
        var traces = data.map(function (t) {
          return { x: t.x, y: t.y, name: t.name, type: chartType };
        });
        Plotly.newPlot('plotly-chart', traces, { margin: { t: 30 } });
      })
      .catch(function (err) { showError('Request failed: ' + err.message); })
      .finally(function () { plotBtn.disabled = false; });
  }

  plotBtn.addEventListener('click', doPlot);

  exprEl.addEventListener('keydown', function (ev) {
    if (ev.ctrlKey && ev.key === 'Enter') {
      ev.preventDefault();
      doPlot();
    }
  });
}());
