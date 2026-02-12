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
  css:raze(
    "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}";
    "body{font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;font-size:14px;line-height:1.5;color:#222;background:#f5f5f5}";
    ".site-header{background:#1a1a2e;color:#e0e0e0;padding:12px 24px}";
    ".site-header h1{font-size:18px;font-weight:600;letter-spacing:0.02em}";
    ".site-main{max-width:1100px;margin:24px auto;padding:0 24px;display:flex;flex-direction:column;gap:20px}";
    ".site-footer{text-align:center;padding:16px;color:#888;font-size:12px}";
    ".card{background:#fff;border:1px solid #ddd;border-radius:4px;padding:20px 24px}";
    ".card h2{font-size:15px;font-weight:600;margin-bottom:14px;padding-bottom:8px;border-bottom:1px solid #eee;color:#333}";
    ".info-grid{display:grid;grid-template-columns:max-content 1fr;gap:4px 16px}";
    ".info-grid dt{font-weight:500;color:#555}";
    ".info-grid dd{font-family:'SFMono-Regular',Consolas,'Liberation Mono',Menlo,monospace;color:#222}";
    ".ns-group{margin-bottom:20px}";
    ".ns-group:last-child{margin-bottom:0}";
    ".ns-group h3{font-size:13px;font-weight:600;color:#555;margin-bottom:8px;font-family:'SFMono-Regular',Consolas,'Liberation Mono',Menlo,monospace}";
    ".obj-table{width:100%;border-collapse:collapse;font-size:13px}";
    ".obj-table th{text-align:left;padding:6px 10px;background:#f8f8f8;border-bottom:1px solid #ddd;font-weight:600;color:#444}";
    ".obj-table td{padding:5px 10px;border-bottom:1px solid #f0f0f0}";
    ".obj-table tr:last-child td{border-bottom:none}";
    ".obj-table tr:hover td{background:#f9f9ff}";
    "code{font-family:'SFMono-Regular',Consolas,'Liberation Mono',Menlo,monospace;background:#f4f4f4;padding:1px 4px;border-radius:2px}"
  );
  raze(
    "<!DOCTYPE html>";
    "<html lang='en'>";
    "<head>";
    "<meta charset='utf-8'>";
    "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    "<title>",ttl,"</title>";
    "<style>",css,"</style>";
    "</head>";
    "<body>";
    "<header class='site-header'><h1>kdb+ process browser</h1></header>";
    "<main class='site-main'>",bodyContent,"</main>";
    "<footer class='site-footer'>kdb+ process browser</footer>";
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
  / count functions: symbols in key`. where type of value >= 100h,
  / excluding tables, views, and system namespace prefixes
  allNames:key`;
  sysNs:`q`Q`z`h`j`o`O;
  allNames:allNames except sysNs;
  tblNames:tables[];
  vwNames:views[];
  / filter to lambdas/projections/operators (type >= 100h)
  fnNames:allNames where 100h<=type each @[value;;(::)] each allNames;
  / exclude tables and views from the function list
  fnNames:fnNames except tblNames,vwNames;
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
  / all names in default namespace, excluding system namespace prefixes
  allNames:key`;
  sysNs:`q`Q`z`h`j`o`O;
  allNames:allNames except sysNs;
  / function names: type >= 100h, excluding tables and views
  fnNames:allNames where 100h<=type each @[value;;(::)] each allNames;
  fnNames:fnNames except tblNames,vwNames;
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
  / combine all rows
  allRows:tblRows,vwRows,fnRows;
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

/ handleRoot: serve the process browser landing page
handleRoot:{[req]
  httpResp["200 OK";"text/html; charset=utf-8";htmlPage["kdb+ process browser";htmlProcessInfo[],htmlObjectBrowser[]]]
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
dispatch:{[parsed]
  sym:`$parsed[`path];
  handler:$[sym in key routes; routes sym; handle404];
  handler parsed
 }

/ .z.ph: route incoming HTTP GET requests
.z.ph:{[x]
  parsed:buildReq x;
  -1 "zph: GET ",parsed[`path];
  @[dispatch; parsed; {[e] -1 "zph ERROR: ",e; httpResp["500 Internal Server Error";"text/html; charset=utf-8";htmlPage["500 Error";"<section class='card'><h2>500 Internal Server Error</h2><pre>",e,"</pre></section>"]]}]
 }

-1 "zph loaded: iteration 3 — router + landing page";
