// app.js — minimal browser REPL for kdbZPH
// Sends q expressions to the eval POST endpoint and displays results

(function () {
  'use strict';

  var exprEl = document.getElementById('expr');
  var runBtn = document.getElementById('run');
  var outputEl = document.getElementById('output');

  if (!exprEl || !runBtn || !outputEl) return;

  function showResult(text) {
    outputEl.textContent = text;
  }

  function runExpr() {
    var expr = exprEl.value.trim();
    if (!expr) {
      showResult('(empty expression)');
      return;
    }

    showResult('...');
    runBtn.disabled = true;

    var body = JSON.stringify({ action: 'eval', expr: expr });

    fetch('/', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: body
    })
      .then(function (resp) { return resp.json(); })
      .then(function (data) {
        if (data.ok) {
          showResult(typeof data.result === 'string'
            ? data.result
            : JSON.stringify(data.result, null, 2));
        } else {
          showResult('ERROR: ' + (data.error || 'unknown error'));
        }
      })
      .catch(function (err) {
        showResult('fetch error: ' + err.message);
      })
      .finally(function () {
        runBtn.disabled = false;
      });
  }

  runBtn.addEventListener('click', runExpr);

  exprEl.addEventListener('keydown', function (ev) {
    if (ev.ctrlKey && ev.key === 'Enter') {
      ev.preventDefault();
      runExpr();
    }
  });
}());
