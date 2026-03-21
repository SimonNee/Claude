httpResp:{[status;ctype;body]
  statusLine:"HTTP/1.1 ",status,"\r\n";
  ctypeLine:"Content-Type: ",ctype,"\r\n";
  clengthLine:"Content-Length: ",(string count body),"\r\n";
  corsLine:"Access-Control-Allow-Origin: *\r\n";
  connLine:"Connection: close\r\n";
  blank:"\r\n";
  statusLine,ctypeLine,clengthLine,corsLine,connLine,blank,body
 }
parseQS:{[qs]
  if[0=count qs; :()!()];
  pairs:"&" vs qs;
  parts:"=" vs/:pairs;
  parts:parts where 2=count each parts;
  if[0=count parts; :()!()];
  (`$parts[;0])!parts[;1]
 }
parseHdr:{[ln]
  pos:first ln ss ": ";
  if[0=count ln ss ": "; :()];
  k:`$pos#ln;
  v:(pos+2)_ln;
  (k;v)
 }
parseReq:{[raw]
  lns:"\r\n" vs raw;
  reqLine:first lns;
  reqParts:" " vs reqLine;
  meth:reqParts 0;
  fullPath:reqParts 1;
  ver:reqParts 2;
  pathParts:"?" vs fullPath;
  pth:pathParts 0;
  qs:$[1<count pathParts; pathParts 1; ""];
  hdrLines:1_lns;
  hdrLines:hdrLines where 0<count each hdrLines;
  hdrPairs:parseHdr each hdrLines;
  hdrPairs:hdrPairs where 0<count each hdrPairs;
  hdrs:$[0=count hdrPairs;
    (`$())!();
    (hdrPairs[;0])!(hdrPairs[;1])
   ];
  `method`path`query`version`headers!(meth;pth;parseQS qs;ver;hdrs)
 }
htmlProcessInfo:{[]
  portVal:string system "p";
  pidVal:string .z.i;
  verVal:(string .z.K)," (",(string .z.k),")";
  osVal:string .z.o;
  heapVal:string .Q.w[][`heap];
  peakVal:string .Q.w[][`peak];
  tblCount:string count tables[];
  fnNames:system "f";
  fnCount:string count fnNames;
  mkRow:{[lbl;val] "<dt>",lbl,"</dt><dd>",val,"</dd>"};
  rows:raze(
    mkRow["Port";portVal];
    mkRow["PID";pidVal];
    mkRow["q Version";verVal];
    mkRow["OS";osVal];
    mkRow["Heap";heapVal];
    mkRow["Peak";peakVal];
    mkRow["Tables";tblCount];
    mkRow["Functions";fnCount]
  );
  "<section id='process-info' class='card'><h2>Process Info</h2><dl class='info-grid'>",rows,"</dl></section>"
 }
htmlObjectBrowser:{[]
  tblNames:tables[];
  vwNames:views[];
  fnNames:(system "f") except tblNames,vwNames;
  varNames:(system "v") except tblNames,vwNames;
  tblRows:raze{[nm]
    tbl:value nm;
    rc:string count tbl;
    cc:string count cols tbl;
    "<tr><td><code>",string[nm],"</code></td><td>table</td><td>",rc,"</td><td>",cc,"</td></tr>"
   }each tblNames;
  vwRows:raze{[nm]
    "<tr><td><code>",string[nm],"</code></td><td>view</td><td>-</td><td>-</td></tr>"
   }each vwNames;
  fnRows:raze{[nm]
    "<tr><td><code>",string[nm],"</code></td><td>function</td><td>-</td><td>-</td></tr>"
   }each fnNames;
  varRows:raze{[nm]
    v:@[value;nm;{(::)}];
    if[(::)~v; :""];
    t:type v;
    tstr:string t;
    rc:$[0>t; "-"; string count v];
    "<tr><td><code>",string[nm],"</code></td><td>",tstr,"</td><td>",rc,"</td><td>-</td></tr>"
   }each varNames;
  allRows:tblRows,vwRows,fnRows,varRows;
  bodyRows:$[0=count allRows;
    "<tr><td colspan='4'>No objects found</td></tr>";
    allRows
   ];
  thead:"<thead><tr><th>Name</th><th>Type</th><th>Rows</th><th>Cols</th></tr></thead>";
  tbl:"<table class='obj-table'>",thead,"<tbody>",bodyRows,"</tbody></table>";
  grp:"<div class='ns-group'><h3>. (default)</h3>",tbl,"</div>";
  "<section id='object-browser' class='card'><h2>Object Browser</h2>",grp,"</section>"
 }
html404:{[pth]
  "<section id='not-found' class='card'><h2>404 &mdash; Not Found</h2><p>No handler for path: <code>",pth,"</code></p><p><a href='/'>Return to dashboard</a></p></section>"
 }
htmlRepl:{[]
  "<section id='repl' class='card'><h2>q REPL</h2><div class='repl-wrap'><div class='repl-header'><span id='ws-status' class='ws-status ws-disconnected'>disconnected</span></div><div id='editor' class='cm-host'></div><div class='repl-controls'><button id='run'>Run</button><span class='repl-hint'>Ctrl+Enter to run</span></div><pre id='output' class='repl-output'></pre></div></section>"
 }
handleRoot:{[req]
  body:htmlProcessInfo[],htmlRepl[],htmlObjectBrowser[];
  httpResp["200 OK";"text/html; charset=utf-8";htmlPage["kdb+ process browser";body]]
 }
handle404:{[req]
  httpResp["404 Not Found";"text/html; charset=utf-8";htmlPage["404 Not Found";html404 req[`path]]]
 }
routes:(enlist`$"/")!enlist handleRoot
buildReq:{[x]
  rawPath:$[10h=type x; x; first x];
  hdrs:$[0h=type x; x 1; (`$())!()];
  rawPath:$[0=count rawPath; enlist"/"; rawPath];
  pathParts:"?" vs rawPath;
  pth:pathParts 0;
  qs:$[1<count pathParts; pathParts 1; ""];
  pth:$["/"~first pth; pth; "/",pth];
  `method`path`query`version`headers!("GET";pth;parseQS qs;"HTTP/1.1";hdrs)
 }
dispatch:{[parsed]
  pth:parsed[`path];
  $["/static/" ~ (count "/static/")#pth;
    handleStatic parsed;
    [sym:`$pth;
     handler:$[sym in key routes; routes sym; handle404];
     handler parsed]
   ]
 }
.z.ph:{[x]
  parsed:buildReq x;
  -1 "zph: GET ",parsed[`path];
  @[dispatch; parsed; {[e] -1 "zph ERROR: ",e; httpResp["500 Internal Server Error";"text/html; charset=utf-8";htmlPage["500 Error";"<section class='card'><h2>500 Internal Server Error</h2><pre>",e,"</pre></section>"]]}]
 }
mimeType:("css";"js";"html";"txt";"json")!("text/css; charset=utf-8";"application/javascript; charset=utf-8";"text/html; charset=utf-8";"text/plain; charset=utf-8";"application/json; charset=utf-8")
handleStatic:{[req]
  filename:(count "/static/")_ req[`path];
  if[".." in "/" vs filename;
    :httpResp["400 Bad Request";"text/plain";"bad path"]
   ];
  fullPath:"static/",filename;
  ext:$["." in filename; last "." vs filename; ""];
  ct:$[ext in key mimeType; mimeType ext; "application/octet-stream"];
  content:@[{"\n" sv read0 hsym`$x}; fullPath; {[e](::)}];
  if[(::)~content;
    :httpResp["404 Not Found";"text/html; charset=utf-8";htmlPage["404 Not Found";html404 req[`path]]]
   ];
  httpResp["200 OK";ct;content]
 }
parsePost:{[x]
  tp:type x;
  if[10h=tp; :`body`headers!(x;(`$())!())];
  body:first x;
  hdrs:$[1<count x; x 1; (`$())!()];
  `body`headers!(body;hdrs)
 }
jsonResp:{[data]
  httpResp["200 OK"; "application/json; charset=utf-8"; .j.j data]
 }
jsonErr:{[msg]
  httpResp["400 Bad Request"; "application/json; charset=utf-8"; .j.j enlist[`error]!enlist msg]
 }
handlePing:{[req]
  jsonResp `status`ts!("ok";string .z.p)
 }
postRoutes:(enlist `ping)!enlist handlePing
.z.pp:{[x]
  .[{[x]
    pp:parsePost x;
    body:pp`body;
    parsed:@[.j.k; body; {[e](::)}];
    if[(::)~parsed; :jsonErr["bad json"]];
    action:`$parsed`action;
    handler:$[action in key postRoutes; postRoutes action; {[r]jsonErr["unknown action"]}];
    handler parsed
   }; enlist x; {[e] jsonErr e}]
 }
evalExpr:{[exprStr]
  lns:"\n" vs exprStr;
  lns:lns where 0<count each lns;
  if[0=count lns; :(1b;(::))];
  if[1=count lns; :@[{(1b;value x)}; first lns; {[e](0b;e)}]];
  res:{[ln] @[{(1b;value x)};ln;{[e](0b;e)}]} each lns;
  errs:where not res[;0];
  if[0<count errs; :res first errs];
  last res
 }
qToJson:{[x]
  tp:type x;
  if[99h=tp;
    if[98h=type value x; :qToJson value x]
   ];
  if[98h=tp;
    limited:(1000&count x)#x;
    :.j.j flip limited
   ];
  if[tp>=100h; :string x];
  if[(tp within (-19;19)) and not 0h=tp; :.j.j x];
  .j.j string each x
 }
handleEval:{[req]
  exprStr:req`expr;
  res:evalExpr exprStr;
  ok:first res;
  payload:$[ok;
    `ok`result!(1b; qToJson last res);
    `ok`error!(0b; last res)
   ];
  jsonResp payload
 }
postRoutes:(`ping`eval)!(handlePing;handleEval)
-1 "zph loaded: iteration 6 — REPL endpoint";
apiTables:{[]
  nms:tables[];
  rows:{[nm]
    tbl:value nm;
    t:$[99h=type tbl; value tbl; tbl];
    `name`rows`cols!(string nm; count t; count cols t)
   }each nms;
  jsonResp rows
 }
apiMeta:{[req]
  qry:req[`query];
  tblName:$[`table in key qry; qry[`table]; ""];
  if[0=count tblName; :jsonErr["table parameter required"]];
  tblSym:`$tblName;
  if[not tblSym in tables[]; :jsonErr["no such table"]];
  jsonResp 0!meta value tblSym
 }
apiData:{[req]
  qry:req[`query];
  tblName:$[`table in key qry; qry[`table]; ""];
  if[0=count tblName; :jsonErr["table parameter required"]];
  tblSym:`$tblName;
  if[not tblSym in tables[]; :jsonErr["no such table"]];
  nRows:"I"$$[`n in key qry; qry[`n]; "100"];
  nRows:$[null nRows; 100i; nRows];
  offsetRows:"I"$$[`offset in key qry; qry[`offset]; "0"];
  offsetRows:$[null offsetRows; 0i; offsetRows];
  tbl:value tblSym;
  tbl:$[99h=type tbl; value tbl; tbl];
  page:(nRows&(count tbl)-offsetRows)#offsetRows _ tbl;
  jsonResp flip page
 }
apiTablesHandler:{[req] apiTables[]}
apiMetaHandler:{[req] apiMeta req}
apiDataHandler:{[req] apiData req}
routes:routes , (`$"/api/tables";`$"/api/meta";`$"/api/data")!(apiTablesHandler;apiMetaHandler;apiDataHandler)
htmlExplorer:{[]
  picker:"<div class='explorer-controls'><label for='tblPicker'>Table:</label> <select id='tblPicker'><option value=''>-- select --</option></select></div>";
  schemaPanel:"<div id='schema' class='explorer-panel'></div>";
  gridPanel:"<div id='grid' class='explorer-panel'></div>";
  "<section id='explorer' class='card'><h2>Data Explorer</h2>",picker,schemaPanel,gridPanel,"</section>"
 }
handleExplorer:{[req]
  httpResp["200 OK";"text/html; charset=utf-8";htmlPage["Data Explorer";htmlExplorer[]]]
 }
routes:routes , (enlist`$"/explorer")!enlist handleExplorer
htmlPage:{[ttl;bodyContent]
  nav:"<nav class='site-nav'><a href='/'>Dashboard</a> <a href='/explorer'>Explorer</a> <a href='/repl'>REPL</a> <a href='/graph'>Graph</a></nav>";
  raze(
    "<!DOCTYPE html>";
    "<html lang='en'>";
    "<head>";
    "<meta charset='utf-8'>";
    "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    "<title>",ttl,"</title>";
    "<link rel='stylesheet' href='/static/style.css'>";
    "</head>";
    "<body>";
    "<header class='site-header'><h1>kdb+ process browser</h1>",nav,"</header>";
    "<main class='site-main'>",bodyContent,"</main>";
    "<footer class='site-footer'>kdb+ process browser</footer>";
    "<script src='https://cdn.plot.ly/plotly-latest.min.js'></script>";
    "<script src='/static/app.js'></script>";
    "<script type='module' src='/static/editor.js'></script>";
    "<script src='/static/graph.js'></script>";
    "</body>";
    "</html>"
  )
 }
-1 "zph loaded: iteration 7 — data explorer";
wsEval:{[msgStr]
  if[not 10h=type msgStr;
    :.j.j `id`ok`error!("";0b;"binary frames not supported")
   ];
  parsed:@[.j.k; msgStr; {[e](::)}];
  if[(::)~parsed; :.j.j `id`ok`error!("";0b;"bad json")];
  msgId:$[`id in key parsed; parsed`id; ""];
  exprStr:$[`expr in key parsed; parsed`expr; ""];
  if[0=count exprStr; :.j.j `id`ok`error!(msgId;0b;"missing expr")];
  res:evalExpr exprStr;
  ok:first res;
  $[ok;
    .j.j `id`ok`result!(msgId;1b;qToJson last res);
    .j.j `id`ok`error!(msgId;0b;last res)
   ]
 }
.z.ws:{[x]
  resp:wsEval x;
  @[neg[.z.w]; resp; {[e] -1 "ws send error: ",e}]
 }
-1 "zph loaded: iteration 8 — WebSocket REPL";
-1 "zph loaded: iteration 9 — code editor";
temporalTypes:"pdztnuv"
plotSerialise:{[col]
  tc:abs type col;
  tchar:.Q.t tc;
  $[tchar in temporalTypes;
    $[tc=12h; string`datetime$col; string col];
    col]
 }
plotTrace:{[xv;yv;nm]
  `x`y`name!(plotSerialise xv; plotSerialise yv; nm)
 }
toPlotData:{[x]
  t:type x;
  if[t<0h; '"toPlotData: atom — wrap in a list"];
  if[(t>0h) and t<20h;
    :enlist plotTrace[til count x; x; "y"]
   ];
  if[t=99h;
    if[98h=type key x; x:0!x; t:98h]
   ];
  if[t=98h;
    cs:cols x;
    if[2>count cs; '"toPlotData: table needs at least 2 columns"];
    xv:x cs 0;
    ycols:1_cs;
    :{[tbl;xv;yc] plotTrace[xv; tbl yc; string yc]}[x;xv] each ycols
   ];
  if[t=99h;
    ks:key x;
    if[2>count ks; '"toPlotData: dict needs at least 2 keys"];
    lens:count each value x;
    if[1<count distinct lens; '"toPlotData: dict values must be equal length"];
    xk:$[`x in ks; `x; first ks];
    ycols:ks except xk;
    xv:x xk;
    :{[d;xv;yk] plotTrace[xv; d yk; string yk]}[x;xv] each ycols
   ];
  if[t=0h;
    if[2<>count x; '"toPlotData: list must have exactly 2 elements"];
    if[not all (type each x) in `short$1+til 19; '"toPlotData: elements must be typed vectors"];
    if[(count x 0)<>count x 1; '"toPlotData: x and y must be equal length"];
    :enlist plotTrace[x 0; x 1; "y"]
   ];
  '"toPlotData: unsupported type"
 }
handlePlot:{[req]
  exprStr:req`expr;
  res:evalExpr exprStr;
  if[not first res; :jsonErr last res];
  traces:@[toPlotData; last res; {[e] e}];
  $[10h=type traces;
    jsonErr traces;
    jsonResp traces
   ]
 }
postRoutes:(`ping`eval`plot)!(handlePing;handleEval;handlePlot)
htmlGraph:{[]
  controls:"<div class='graph-controls'><label for='graph-expr'>Expression:</label><textarea id='graph-expr' rows='3' placeholder='([]x:til 10;y:til 10)'></textarea><div class='graph-row'><label for='chart-type'>Chart:</label><select id='chart-type'><option value='line'>Line</option><option value='bar'>Bar</option><option value='scatter'>Scatter</option></select><button id='plot-btn'>Plot</button><span class='repl-hint'>Ctrl+Enter to plot</span></div></div>";
  chartArea:"<div class='plotly-wrap'><div id='plotly-chart'></div></div>";
  "<section id='graph' class='card'><h2>Graph</h2>",controls,chartArea,"</section>"
 }
handleGraph:{[req]
  httpResp["200 OK";"text/html; charset=utf-8";htmlPage["Graph";htmlGraph[]]]
 }
routes:routes , (enlist`$"/graph")!enlist handleGraph
-1 "zph loaded: iteration 10 — visualization";
