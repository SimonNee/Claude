// editor.js — CodeMirror 6 REPL editor
// ES module loaded via <script type="module"> after app.js
// Exposes window.getExpr and window.setExpr for app.js to call

import { EditorView, keymap, highlightActiveLine } from 'https://esm.sh/@codemirror/view@6';
import { EditorState } from 'https://esm.sh/@codemirror/state@6';
import { defaultKeymap, historyKeymap, history } from 'https://esm.sh/@codemirror/commands@6';
import { syntaxHighlighting, defaultHighlightStyle } from 'https://esm.sh/@codemirror/language@6';

var hostEl = document.getElementById('editor');
if (!hostEl) {
  // not on the REPL page — nothing to do
} else {
  var HISTORY_KEY = 'kdbzph_repl_history';
  var hist = [];
  try { hist = JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]'); } catch (e) { hist = []; }
  var histIdx = hist.length;

  window.__cmView = null;

  window.getExpr = function () {
    return window.__cmView ? window.__cmView.state.doc.toString() : '';
  };

  window.setExpr = function (text) {
    if (!window.__cmView) return;
    window.__cmView.dispatch({
      changes: { from: 0, to: window.__cmView.state.doc.length, insert: text }
    });
  };

  function submitExpr() {
    var expr = window.getExpr().trim();
    if (!expr) return false; // let CM handle Enter normally when editor is empty
    // save to history (avoid duplicate consecutive entries)
    if (hist[hist.length - 1] !== expr) {
      hist.push(expr);
      try { localStorage.setItem(HISTORY_KEY, JSON.stringify(hist.slice(-100))); } catch (e) {}
    }
    histIdx = hist.length;
    // trigger the Run button — app.js listens there and sends via WebSocket
    var btn = document.getElementById('run');
    if (btn) btn.click();
    return true;
  }

  function historyUp() {
    if (hist.length === 0) return false;
    histIdx = Math.max(0, histIdx - 1);
    window.setExpr(hist[histIdx]);
    return true;
  }

  function historyDown() {
    if (histIdx >= hist.length - 1) {
      histIdx = hist.length;
      window.setExpr('');
      return true;
    }
    histIdx += 1;
    window.setExpr(hist[histIdx]);
    return true;
  }

  var startState = EditorState.create({
    doc: '',
    extensions: [
      history(),
      syntaxHighlighting(defaultHighlightStyle),
      highlightActiveLine(),
      keymap.of([
        // Enter alone submits; Shift-Enter inserts a literal newline
        { key: 'Enter', run: submitExpr },
        { key: 'Shift-Enter', run: function (view) {
          view.dispatch(view.state.replaceSelection('\n'));
          return true;
        }},
        // ArrowUp/Down cycle through localStorage history
        { key: 'ArrowUp', run: historyUp },
        { key: 'ArrowDown', run: historyDown },
        ...defaultKeymap,
        ...historyKeymap
      ])
    ]
  });

  window.__cmView = new EditorView({
    state: startState,
    parent: hostEl
  });
}
