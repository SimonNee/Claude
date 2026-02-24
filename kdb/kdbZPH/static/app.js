// app.js — browser frontend for kdbZPH
// Iteration 6: REPL (fetch-based eval)
// Iteration 7: Data Explorer (table picker, schema panel, data grid)

// ---- REPL ----
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

// ---- Data Explorer ----
(function () {
  'use strict';

  var pickerEl = document.getElementById('tblPicker');
  var schemaEl = document.getElementById('schema');
  var gridEl = document.getElementById('grid');

  // only run on the explorer page
  if (!pickerEl || !schemaEl || !gridEl) return;

  // populate table picker from /api/tables
  fetch('/api/tables')
    .then(function (resp) { return resp.json(); })
    .then(function (tables) {
      tables.forEach(function (tbl) {
        var opt = document.createElement('option');
        opt.value = tbl.name;
        opt.textContent = tbl.name + ' (' + tbl.rows + ' rows, ' + tbl.cols + ' cols)';
        pickerEl.appendChild(opt);
      });
    })
    .catch(function (err) {
      schemaEl.textContent = 'Failed to load tables: ' + err.message;
    });

  // render schema table from /api/meta?table=X
  function loadSchema(tblName) {
    schemaEl.innerHTML = '<p>Loading schema...</p>';
    fetch('/api/meta?table=' + encodeURIComponent(tblName))
      .then(function (resp) { return resp.json(); })
      .then(function (data) {
        if (data.error) {
          schemaEl.innerHTML = '<p class="error">Error: ' + data.error + '</p>';
          return;
        }
        var cols = data.c || [];
        var types = data.t || [];
        var html = '<h3>Schema: ' + tblName + '</h3>';
        html += '<table class="obj-table"><thead><tr><th>Column</th><th>Type</th></tr></thead><tbody>';
        for (var i = 0; i < cols.length; i++) {
          html += '<tr><td><code>' + cols[i] + '</code></td><td>' + (types[i] || '') + '</td></tr>';
        }
        html += '</tbody></table>';
        schemaEl.innerHTML = html;
      })
      .catch(function (err) {
        schemaEl.innerHTML = '<p class="error">Schema error: ' + err.message + '</p>';
      });
  }

  // render data grid from /api/data?table=X&n=100
  function loadData(tblName) {
    gridEl.innerHTML = '<p>Loading data...</p>';
    fetch('/api/data?table=' + encodeURIComponent(tblName) + '&n=100')
      .then(function (resp) { return resp.json(); })
      .then(function (data) {
        if (data.error) {
          gridEl.innerHTML = '<p class="error">Error: ' + data.error + '</p>';
          return;
        }
        // data is column-oriented: {col1:[...], col2:[...]}
        var colNames = Object.keys(data);
        if (colNames.length === 0) {
          gridEl.innerHTML = '<p>(empty table)</p>';
          return;
        }
        var rowCount = data[colNames[0]].length;
        var html = '<h3>Data: ' + tblName + ' (first ' + rowCount + ' rows)</h3>';
        html += '<div class="grid-scroll"><table class="obj-table"><thead><tr>';
        colNames.forEach(function (c) { html += '<th>' + c + '</th>'; });
        html += '</tr></thead><tbody>';
        for (var r = 0; r < rowCount; r++) {
          html += '<tr>';
          colNames.forEach(function (c) {
            html += '<td>' + (data[c][r] !== null ? data[c][r] : 'null') + '</td>';
          });
          html += '</tr>';
        }
        html += '</tbody></table></div>';
        gridEl.innerHTML = html;
      })
      .catch(function (err) {
        gridEl.innerHTML = '<p class="error">Data error: ' + err.message + '</p>';
      });
  }

  pickerEl.addEventListener('change', function () {
    var tblName = pickerEl.value;
    if (!tblName) {
      schemaEl.innerHTML = '';
      gridEl.innerHTML = '';
      return;
    }
    loadSchema(tblName);
    loadData(tblName);
  });
}());
