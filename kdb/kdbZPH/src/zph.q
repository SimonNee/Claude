/ zph.q — minimal .z.ph HTTP handler
/ Iteration 1: returns "Hello World!" to any GET request
/ Iteration 2: parseQS, parseReq, echo parsed request back
/ Iteration 3: router + landing page (process browser)

/ httpResp: build a complete HTTP/1.1 response string
/ args:
/   status  - status line text, e.g. "200 OK"
/   ctype   - content-type value, e.g. "text/html"
/   body    - response body string
/ returns: full HTTP response string ready to send
httpResp:{[status;ctype;body]
  statusLine:"HTTP/1.1 ",status,"\r\n";
  ctypeLine:"Content-Type: ",ctype,"\r\n";
  clengthLine:"Content-Length: ",(string count body),"\r\n";
  connLine:"Connection: close\r\n";
  blank:"\r\n";
  statusLine,ctypeLine,clengthLine,connLine,blank,body
 }

/ parseQS: parse a query string into a dictionary
/ arg: qs - query string e.g. "name=trade&n=10"
/ returns: dict with symbol keys and string values
/   e.g. `name`n!("trade";"10")
/ returns ()!() for empty input
parseQS:{[qs]
  / empty input: return empty dict
  if[0=count qs; :()!()];
  / split on "&" to get individual param=value pairs
  pairs:"&" vs qs;
  / split each pair on "=" — keep only pairs with exactly two parts
  parts:"=" vs/:pairs;
  parts:parts where 2=count each parts;
  / if nothing survived the filter, return empty dict
  if[0=count parts; :()!()];
  / build dict: symbol keys, string values
  (`$parts[;0])!parts[;1]
 }

/ parseHdr: parse a single header line "Key: value" into a (sym;string) pair
/ Splits on the FIRST ": " only, so values containing ":" are preserved
/ returns 2-element list (`Key;"value") or () if no ": " found
parseHdr:{[ln]
  pos:first ln ss ": ";
  / ss returns empty list if not found; skip malformed lines
  if[0=count ln ss ": "; :()];
  k:`$pos#ln;
  v:(pos+2)_ln;
  (k;v)
 }

/ parseReq: parse a raw HTTP request string into a structured dictionary
/ arg: raw - full HTTP request string
/ returns dict with keys: method path query version headers
/ example:
/   method  -> "GET"
/   path    -> "/api/table"
/   query   -> `name`n!("trade";"10")
/   version -> "HTTP/1.1"
/   headers -> `Host`Accept!("localhost:5050";"text/html")
parseReq:{[raw]
  / split the request on CRLF to get individual lines
  lns:"\r\n" vs raw;
  / first line is the request line: "METHOD /path?qs VERSION"
  reqLine:first lns;
  reqParts:" " vs reqLine;
  meth:reqParts 0;
  fullPath:reqParts 1;
  ver:reqParts 2;
  / split full path on "?" to separate path from query string
  / "?" vs "/path" gives enlist "/path" (no query string)
  / "?" vs "/path?" gives ("/path";"") (empty query string)
  / "?" vs "/path?a=1" gives ("/path";"a=1")
  pathParts:"?" vs fullPath;
  pth:pathParts 0;
  qs:$[1<count pathParts; pathParts 1; ""];
  / remaining lines: skip first (request line) and any empty lines
  hdrLines:1_lns;
  hdrLines:hdrLines where 0<count each hdrLines;
  / parse each header line and filter out failed parses
  hdrPairs:parseHdr each hdrLines;
  hdrPairs:hdrPairs where 0<count each hdrPairs;
  / build headers dict: symbol keys, string values
  hdrs:$[0=count hdrPairs;
    (`$())!();
    (hdrPairs[;0])!(hdrPairs[;1])
   ];
  / assemble result dictionary
  `method`path`query`version`headers!(meth;pth;parseQS qs;ver;hdrs)
 }

/ .
/ Iteration 3: HTML page builder, object browser, router
/ .

/ htmlPage: wrap body content in a full HTML document
/ args:
/   ttl         - page title string
/   bodyContent - HTML string for the <main> element
/ returns: full HTML document string
htmlPage:{[ttl;bodyContent]
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
    "<header class='site-header'><h1>kdb+ process browser</h1></header>";
    "<main class='site-main'>",bodyContent,"</main>";
    "<footer class='site-footer'>kdb+ process browser</footer>";
    "<script src='/static/app.js'></script>";
    "</body>";
    "</html>"
  )
 }

/ htmlProcessInfo: build the process info card
/ returns: HTML string for the process info section
htmlProcessInfo:{[]
  / collect each dt/dd pair as a string
  / Port: system "p" returns the current listening port
  / .z.i: process ID
  / .z.K: q release version (float), .z.k: release date
  / .z.o: OS name
  / .Q.w[]: memory stats dictionary
  / tables[]: list of tables in default namespace
  / functions: names in . namespace where type >= 100h (lambda/projection/etc)
  portVal:string system "p";
  pidVal:string .z.i;
  verVal:(string .z.K)," (",(string .z.k),")";
  osVal:string .z.o;
  heapVal:string .Q.w[][`heap];
  peakVal:string .Q.w[][`peak];
  tblCount:string count tables[];
  / count user-defined functions via system "f"
  fnNames:system "f";
  fnCount:string count fnNames;
  / build rows
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

/ htmlObjectBrowser: build the object browser card
/ returns: HTML string for the object browser section
htmlObjectBrowser:{[]
  tblNames:tables[];
  vwNames:views[];
  / use system "f" and system "v" to enumerate user-defined objects
  fnNames:(system "f") except tblNames,vwNames;
  varNames:(system "v") except tblNames,vwNames;
  / build one table row per object
  / tables: show row count and column count
  tblRows:raze{[nm]
    tbl:value nm;
    rc:string count tbl;
    cc:string count cols tbl;
    "<tr><td><code>",string[nm],"</code></td><td>table</td><td>",rc,"</td><td>",cc,"</td></tr>"
   }each tblNames;
  / views: show "-" for rows and cols
  vwRows:raze{[nm]
    "<tr><td><code>",string[nm],"</code></td><td>view</td><td>-</td><td>-</td></tr>"
   }each vwNames;
  / functions: show "-" for rows and cols
  fnRows:raze{[nm]
    "<tr><td><code>",string[nm],"</code></td><td>function</td><td>-</td><td>-</td></tr>"
   }each fnNames;
  / variables: show type number and count (or "-" for atoms)
  varRows:raze{[nm]
    v:@[value;nm;{(::)}];
    if[(::)~v; :""];
    t:type v;
    tstr:string t;
    rc:$[0>t; "-"; string count v];
    "<tr><td><code>",string[nm],"</code></td><td>",tstr,"</td><td>",rc,"</td><td>-</td></tr>"
   }each varNames;
  / combine all rows
  allRows:tblRows,vwRows,fnRows,varRows;
  / if nothing exists, show placeholder row
  bodyRows:$[0=count allRows;
    "<tr><td colspan='4'>No objects found</td></tr>";
    allRows
   ];
  thead:"<thead><tr><th>Name</th><th>Type</th><th>Rows</th><th>Cols</th></tr></thead>";
  tbl:"<table class='obj-table'>",thead,"<tbody>",bodyRows,"</tbody></table>";
  grp:"<div class='ns-group'><h3>. (default)</h3>",tbl,"</div>";
  "<section id='object-browser' class='card'><h2>Object Browser</h2>",grp,"</section>"
 }

/ html404: build a 404 not-found card
/ arg: pth - the requested path string
/ returns: HTML string for the 404 section
html404:{[pth]
  "<section id='not-found' class='card'><h2>404 &mdash; Not Found</h2><p>No handler for path: <code>",pth,"</code></p><p><a href='/'>Return to dashboard</a></p></section>"
 }

/ htmlRepl: build the REPL section card
/ returns: HTML string for the REPL section
htmlRepl:{[]
  "<section id='repl' class='card'><h2>q REPL</h2><div class='repl-wrap'><textarea id='expr' rows='3' placeholder='1+1'></textarea><div class='repl-controls'><button id='run'>Run</button><span class='repl-hint'>Ctrl+Enter to run</span></div><pre id='output' class='repl-output'></pre></div></section>"
 }

/ handleRoot: serve the process browser landing page
handleRoot:{[req]
  body:htmlProcessInfo[],htmlRepl[],htmlObjectBrowser[];
  httpResp["200 OK";"text/html; charset=utf-8";htmlPage["kdb+ process browser";body]]
 }

/ handle404: serve the 404 not-found page
handle404:{[req]
  httpResp["404 Not Found";"text/html; charset=utf-8";htmlPage["404 Not Found";html404 req[`path]]]
 }

/ routes: dictionary mapping path symbols to handler functions
routes:(enlist`$"/")!enlist handleRoot

/ buildReq: build a parsed request dict from KDB+'s .z.ph input
/ KDB+ passes either a string (path+query) or (path+query; headerDict)
/ For root path, KDB+ passes "" not "/"
buildReq:{[x]
  / extract path+query string and headers
  rawPath:$[10h=type x; x; first x];
  hdrs:$[0h=type x; x 1; (`$())!()];
  / empty path means root — enlist to keep it a string (type 10h)
  rawPath:$[0=count rawPath; enlist"/"; rawPath];
  / split path from query string
  pathParts:"?" vs rawPath;
  pth:pathParts 0;
  qs:$[1<count pathParts; pathParts 1; ""];
  / ensure path starts with /
  pth:$["/"~first pth; pth; "/",pth];
  `method`path`query`version`headers!("GET";pth;parseQS qs;"HTTP/1.1";hdrs)
 }

/ dispatch: route a parsed request dict to the correct handler
/ prefix route: /static/* goes to handleStatic before dict lookup
dispatch:{[parsed]
  pth:parsed[`path];
  $["/static/" ~ (count "/static/")#pth;
    handleStatic parsed;
    [sym:`$pth;
     handler:$[sym in key routes; routes sym; handle404];
     handler parsed]
   ]
 }

/ .z.ph: route incoming HTTP GET requests
.z.ph:{[x]
  parsed:buildReq x;
  -1 "zph: GET ",parsed[`path];
  @[dispatch; parsed; {[e] -1 "zph ERROR: ",e; httpResp["500 Internal Server Error";"text/html; charset=utf-8";htmlPage["500 Error";"<section class='card'><h2>500 Internal Server Error</h2><pre>",e,"</pre></section>"]]}]
 }

/ .
/ Iteration 4: static file server
/ .

/ mimeType: map file extension strings to MIME type strings
mimeType:("css";"js";"html";"txt";"json")!("text/css; charset=utf-8";"application/javascript; charset=utf-8";"text/html; charset=utf-8";"text/plain; charset=utf-8";"application/json; charset=utf-8")

/ handleStatic: serve files from the static/ directory
/ arg: req - parsed request dict (from buildReq)
/ returns: HTTP response string (200 with file, 400 bad path, or 404 not found)
handleStatic:{[req]
  / strip the /static/ prefix to get the relative filename
  filename:(count "/static/")_ req[`path];
  / path traversal check: reject any component equal to ".."
  if[".." in "/" vs filename;
    :httpResp["400 Bad Request";"text/plain";"bad path"]
   ];
  / build full filesystem path (relative to process working directory)
  fullPath:"static/",filename;
  / extract file extension: everything after the last "."
  ext:$["." in filename; last "." vs filename; ""];
  / look up MIME type; fall back to octet-stream for unknown extensions
  ct:$[ext in key mimeType; mimeType ext; "application/octet-stream"];
  / attempt to read the file; on error return generic null (::)
  content:@[{"\n" sv read0 hsym`$x}; fullPath; {[e](::)}];
  / if file not found or unreadable, return 404
  if[(::)~content;
    :httpResp["404 Not Found";"text/html; charset=utf-8";htmlPage["404 Not Found";html404 req[`path]]]
   ];
  httpResp["200 OK";ct;content]
 }

/ .
/ Iteration 5: POST handler + JSON layer
/ .

/ parsePost: extract body string and headers from .z.pp argument
/ .z.pp receives either a string (body only) or (body; headerDict)
/ also handles enlist-wrapped single-element list for direct testing
/ returns: dict with keys `body (string) and `headers (dict)
parsePost:{[x]
  tp:type x;
  / plain string: body is x, no headers
  if[10h=tp; :`body`headers!(x;(`$())!())];
  / general list: first element is body string
  / 1 element: no headers; 2+ elements: second is header dict
  body:first x;
  hdrs:$[1<count x; x 1; (`$())!()];
  `body`headers!(body;hdrs)
 }

/ jsonResp: wrap data in HTTP 200 application/json response with CORS header
/ arg: data - any q value serialisable by .j.j
/ returns: full HTTP response string
jsonResp:{[data]
  body:.j.j data;
  "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: ",(string count body),"\r\nConnection: close\r\n\r\n",body
 }

/ jsonErr: return HTTP 400 response with JSON error body and CORS header
/ arg: msg - error message string
/ returns: full HTTP 400 response string
jsonErr:{[msg]
  body:.j.j enlist[`error]!enlist msg;
  "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: ",(string count body),"\r\nConnection: close\r\n\r\n",body
 }

/ handlePing: respond to ping action with status ok and current timestamp
/ arg: req - parsed JSON dict (from .j.k)
/ returns: jsonResp string
handlePing:{[req]
  jsonResp `status`ts!("ok";string .z.p)
 }

/ postRoutes: dict mapping action symbols to handler functions
postRoutes:(enlist `ping)!enlist handlePing

/ .z.pp: entry point for HTTP POST requests
/ routes on "action" key in JSON body — NOT on URL path
/ .z.pp does not receive the URL path at all
.z.pp:{[x]
  .[{[x]
    pp:parsePost x;
    body:pp`body;
    / trap malformed JSON; (::) signals parse failure
    parsed:@[.j.k; body; {[e](::)}];
    if[(::)~parsed; :jsonErr["bad json"]];
    / extract action key and dispatch
    action:`$parsed`action;
    handler:$[action in key postRoutes; postRoutes action; {[r]jsonErr["unknown action"]}];
    handler parsed
   }; enlist x; {[e] jsonErr e}]
 }

/ .
/ Iteration 6: q REPL endpoint
/ .

/ evalExpr: safely evaluate a q expression string
/ arg: exprStr - q expression as a string (type 10h)
/ returns: (1b; result) on success, (0b; errorString) on failure
/ NOTE: uses value (not reval) — local single-user workbench assumption
evalExpr:{[exprStr]
  @[{(1b;value x)}; exprStr; {[e](0b;e)}]
 }

/ qToJson: convert any q result to a JSON string
/ Handles all q types with appropriate fallbacks:
/   tables (98h)          -> column-oriented JSON via flip + .j.j, 1000-row limit
/   keyed tables (99h)    -> unkey via value, then treat as table
/   atoms/simple lists    -> .j.j directly (types < 20h, not mixed)
/   functions/lambdas     -> string for source representation (type >= 100h)
/   mixed lists / other   -> string each as universal fallback
/ arg: x - any q value
/ returns: JSON string (type 10h)
qToJson:{[x]
  tp:type x;
  / keyed table: 99h where value is a table (98h)
  if[99h=tp;
    if[98h=type value x; :qToJson value x]
   ];
  / plain table
  if[98h=tp;
    limited:(1000&count x)#x;
    :.j.j flip limited
   ];
  / functions and lambdas: type >= 100h
  if[tp>=100h; :string x];
  / atoms and uniform lists: type in -19h to 19h (but not 0h mixed)
  / type 0h is mixed list — falls through to fallback
  if[(tp within (-19;19)) and not 0h=tp; :.j.j x];
  / fallback: mixed lists, dicts with mixed values, anything else
  .j.j string each x
 }

/ handleEval: POST action "eval" — evaluate a q expression and return result
/ arg: req - parsed JSON dict with key "expr" (q expression string)
/ returns: HTTP 200 JSON response with ok:true+result or ok:false+error
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

/ add eval to postRoutes
postRoutes:(`ping`eval)!(handlePing;handleEval)

-1 "zph loaded: iteration 6 — REPL endpoint";

/ .
/ Iteration 7: data explorer
/ .

/ apiTables: return a list of dicts with table metadata for all default-namespace tables
/ each entry: `name`rows`cols!(nameStr; rowCount; colCount)
/ returns: jsonResp wrapping the list
apiTables:{[]
  nms:tables[];
  / for each table name, build a metadata dict
  rows:{[nm]
    tbl:value nm;
    / unkey keyed tables before counting rows
    t:$[99h=type tbl; value tbl; tbl];
    `name`rows`cols!(string nm; count t; count cols t)
   }each nms;
  jsonResp rows
 }

/ apiMeta: return schema for a single named table
/ req: parsed request dict; expects req[`query][`table] = table name string
/ returns: jsonResp with meta columns (c, t, f, a); or jsonErr if table not found
/ Note: meta t column is char (e.g. "j","f","p") — .j.j serialises correctly as-is
apiMeta:{[req]
  qry:req[`query];
  / extract table name string; if key missing, return error
  tblName:$[`table in key qry; qry[`table]; ""];
  if[0=count tblName; :jsonErr["table parameter required"]];
  tblSym:`$tblName;
  / validate: table must exist in default namespace
  if[not tblSym in tables[]; :jsonErr["no such table"]];
  / meta returns a keyed table; 0! removes the key before serialising
  jsonResp 0!meta value tblSym
 }

/ apiData: return paginated rows from a table as column-oriented JSON
/ req: parsed request dict; expects query params: table, n (default 100), offset (default 0)
/ returns: jsonResp with column-oriented data; or jsonErr if table not found
apiData:{[req]
  qry:req[`query];
  tblName:$[`table in key qry; qry[`table]; ""];
  if[0=count tblName; :jsonErr["table parameter required"]];
  tblSym:`$tblName;
  if[not tblSym in tables[]; :jsonErr["no such table"]];
  / parse n and offset from query string; default 100 and 0
  / query values are strings (e.g. "100") — "I"$ casts string to int
  nRows:"I"$$[`n in key qry; qry[`n]; "100"];
  nRows:$[null nRows; 100i; nRows];
  offsetRows:"I"$$[`offset in key qry; qry[`offset]; "0"];
  offsetRows:$[null offsetRows; 0i; offsetRows];
  / retrieve the table; unkey keyed tables
  tbl:value tblSym;
  tbl:$[99h=type tbl; value tbl; tbl];
  / apply pagination: drop offsetRows, then take nRows
  page:nRows#offsetRows _ tbl;
  / return column-oriented JSON
  jsonResp flip page
 }

/ handler wrappers (each takes req and delegates to pure function)
apiTablesHandler:{[req] apiTables[]}
apiMetaHandler:{[req] apiMeta req}
apiDataHandler:{[req] apiData req}

/ wire new GET routes
routes:routes , (`$"/api/tables";`$"/api/meta";`$"/api/data")!(apiTablesHandler;apiMetaHandler;apiDataHandler)

/ htmlExplorer: build the explorer page section
/ returns: HTML string with table picker, schema panel, and data grid
htmlExplorer:{[]
  picker:"<div class='explorer-controls'><label for='tblPicker'>Table:</label> <select id='tblPicker'><option value=''>-- select --</option></select></div>";
  schemaPanel:"<div id='schema' class='explorer-panel'></div>";
  gridPanel:"<div id='grid' class='explorer-panel'></div>";
  "<section id='explorer' class='card'><h2>Data Explorer</h2>",picker,schemaPanel,gridPanel,"</section>"
 }

/ handleExplorer: serve the explorer HTML page
handleExplorer:{[req]
  httpResp["200 OK";"text/html; charset=utf-8";htmlPage["Data Explorer";htmlExplorer[]]]
 }

/ wire the /explorer route
routes:routes , (enlist`$"/explorer")!enlist handleExplorer

/ update htmlPage to include nav links between pages
/ redefine htmlPage to add nav after <h1>
htmlPage:{[ttl;bodyContent]
  nav:"<nav class='site-nav'><a href='/'>Dashboard</a> <a href='/explorer'>Explorer</a> <a href='/repl'>REPL</a></nav>";
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
    "<script src='/static/app.js'></script>";
    "</body>";
    "</html>"
  )
 }

-1 "zph loaded: iteration 7 — data explorer";
