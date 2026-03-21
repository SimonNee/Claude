// app.js — browser frontend for kdbZPH
// Iteration 6: REPL (fetch-based eval)
// Iteration 7: Data Explorer (table picker, schema panel, data grid)
// Iteration 8: REPL (WebSocket-based eval)

// ---- REPL (WebSocket) ----
(function () {
  'use strict';

  // detect REPL page by the CodeMirror host div (replaces the old textarea)
  var editorEl = document.getElementById('editor');
  var runBtn = document.getElementById('run');
  var outputEl = document.getElementById('output');
  var statusEl = document.getElementById('ws-status');

  if (!editorEl || !runBtn || !outputEl) return;

  var ws = null;
  var idCounter = 0;

  function setStatus(text, cls) {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.className = 'ws-status ' + cls;
  }

  function connect() {
    var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    ws = new WebSocket(proto + location.host + '/');
    setStatus('connecting...', 'ws-connecting');

    ws.onopen = function () {
      setStatus('connected', 'ws-connected');
      runBtn.disabled = false;
    };

    ws.onclose = function () {
      setStatus('disconnected — reconnecting in 3s', 'ws-disconnected');
      runBtn.disabled = true;
      ws = null;
      setTimeout(connect, 3000);
    };

    ws.onerror = function () {
      setStatus('error', 'ws-error');
    };

    ws.onmessage = function (ev) {
      var data;
      try { data = JSON.parse(ev.data); } catch (e) {
        outputEl.textContent = 'parse error: ' + ev.data;
        runBtn.disabled = false;
        return;
      }
      if (data.ok) {
        outputEl.textContent = typeof data.result === 'string'
          ? data.result
          : JSON.stringify(data.result, null, 2);
      } else {
        outputEl.textContent = 'ERROR: ' + (data.error || 'unknown error');
      }
      runBtn.disabled = false;
    };
  }

  function runExpr() {
    // editor.js (ES module) sets window.getExpr when CodeMirror is ready
    var expr = typeof window.getExpr === 'function' ? window.getExpr().trim() : '';
    if (!expr) {
      outputEl.textContent = '(empty expression)';
      return;
    }
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      outputEl.textContent = 'WebSocket not connected';
      return;
    }
    idCounter += 1;
    outputEl.textContent = '...';
    runBtn.disabled = true;
    ws.send(JSON.stringify({ id: String(idCounter), expr: expr }));
  }

  runBtn.disabled = true;
  connect();

  // key handling (Ctrl+Enter, ArrowUp/Down, Enter) is managed by editor.js
  // the Run button click is the single entry point for expression submission
  runBtn.addEventListener('click', runExpr);
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
