/ test_zph.q — test suite for zph.q .z.ph handler
/ Iteration 1: Hello World HTTP response tests
/ Iteration 2: parseQS and parseReq tests
/ Iteration 3: router and HTML page builder tests

\l src/zph.q

/ Test harness
pass:0
fail:0

/ assert: evaluate condition, print PASS/FAIL, update counters
assert:{[msg;cond]
  $[cond;
    [pass+::1; -1 "PASS: ",msg];
    [fail+::1; -1 "FAIL: ",msg]
   ]
 }

/ strContains: true if needle appears in haystack at least once
strContains:{[haystack;needle] 0<count ss[haystack;needle]}

/ .
/ Iteration 1 tests
/ .

/ Build a sample response using KDB+'s real input format: (pathQuery; headerDict)
resp:.z.ph(""; (enlist`Host)!enlist"localhost")

/ Test 1: .z.ph returns a string (type 10h)
assert["response is a string (type 10h)"; 10h=type resp]

/ Test 2: response starts with "HTTP/1.1 200"
assert["response starts with HTTP/1.1 200"; "HTTP/1.1 200"~(count "HTTP/1.1 200")#resp]

/ Test 3: response contains Content-Type: text/html
assert["response contains Content-Type: text/html"; strContains[resp;"Content-Type: text/html"]]

/ Test 4: response contains Content-Length:
assert["response contains Content-Length:"; strContains[resp;"Content-Length:"]]

/ Test 5: response body contains DOCTYPE (iteration 3 returns full HTML)
assert["response body contains <!DOCTYPE html>"; strContains[resp;"<!DOCTYPE html>"]]

/ Test 6: headers and body separated by \r\n\r\n
assert["headers and body separated by CRLFCRLF"; strContains[resp;"\r\n\r\n"]]

/ Test 7: Content-Length value matches actual body length
/ Locate the blank-line separator and take everything after it as the body
separatorPos:first ss[resp;"\r\n\r\n"]
body:(separatorPos+4) _ resp
/ Locate the Content-Length header and extract its numeric value
clPos:first ss[resp;"Content-Length: "]
/ "Content-Length: " is 16 characters; the value starts right after
clValStart:clPos+16
/ find the end of the value by locating \r relative to the value start
clValRemainder:clValStart _ resp
crOffset:first ss[clValRemainder;"\r"]
clVal:"J"$(crOffset#clValRemainder)
assert["Content-Length matches actual body length"; clVal=count body]

/ .
/ Iteration 2 tests — parseQS
/ .

/ Test 8: parseQS empty string returns empty dict
assert["parseQS empty string returns empty dict"; (()!())~parseQS ""]

/ Test 9: parseQS single param returns correct key
qs1:parseQS "sym=AAPL"
assert["parseQS single param has key sym"; `sym in key qs1]

/ Test 10: parseQS single param returns correct value
assert["parseQS single param value is AAPL"; "AAPL"~qs1[`sym]]

/ Test 11: parseQS multiple params returns correct dict
qs2:parseQS "sym=AAPL&n=10"
assert["parseQS multiple params has 2 keys"; 2=count key qs2]

/ Test 12: parseQS multiple params — sym key value
assert["parseQS sym=AAPL in multi-param"; "AAPL"~qs2[`sym]]

/ Test 13: parseQS multiple params — n key value
assert["parseQS n=10 in multi-param"; "10"~qs2[`n]]

/ Test 14: parseQS empty query string (after "?") returns empty dict
assert["parseQS empty qs string returns empty dict"; (()!())~parseQS ""]

/ .
/ Iteration 2 tests — parseReq
/ .

/ Build a sample parsed request for tests
sampleReq:"GET /api/table?name=trade&n=10 HTTP/1.1\r\nHost: localhost:5050\r\nAccept: text/html\r\n\r\n"
parsed:parseReq sampleReq

/ Test 15: method is GET
assert["parseReq method is GET"; "GET"~parsed[`method]]

/ Test 16: path is /api/table (no query string)
assert["parseReq path is /api/table"; "/api/table"~parsed[`path]]

/ Test 17: query dict has key name
assert["parseReq query has key name"; `name in key parsed[`query]]

/ Test 18: query dict name value is trade
assert["parseReq query name=trade"; "trade"~parsed[`query][`name]]

/ Test 19: query dict has key n
assert["parseReq query has key n"; `n in key parsed[`query]]

/ Test 20: query dict n value is 10
assert["parseReq query n=10"; "10"~parsed[`query][`n]]

/ Test 21: version is HTTP/1.1
assert["parseReq version is HTTP/1.1"; "HTTP/1.1"~parsed[`version]]

/ Test 22: headers dict has Host key
assert["parseReq headers has Host key"; `Host in key parsed[`headers]]

/ Test 23: headers Host value is localhost:5050
assert["parseReq headers Host=localhost:5050"; "localhost:5050"~parsed[`headers][`Host]]

/ Test 24: headers dict has Accept key
assert["parseReq headers has Accept key"; `Accept in key parsed[`headers]]

/ .
/ parseReq with no query string
/ .

/ Test 25: path-only request — path is correct
noQSReq:"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"
parsedNoQS:parseReq noQSReq
assert["parseReq no-QS path is /index.html"; "/index.html"~parsedNoQS[`path]]

/ Test 26: path-only request — query is empty dict
assert["parseReq no-QS query is empty dict"; (()!())~parsedNoQS[`query]]

/ Test 27: path-only request — method is GET
assert["parseReq no-QS method is GET"; "GET"~parsedNoQS[`method]]

/ .
/ Response echo test
/ .

/ Test 28: .z.ph response to unrouted path returns HTTP/1.1 404 (iteration 3 router)
echoResp:.z.ph("/api/table?name=trade&n=10"; (enlist`Host)!enlist"localhost:5050")
assert["unrouted path returns HTTP/1.1 404"; "HTTP/1.1 404"~(count "HTTP/1.1 404")#echoResp]

/ Test 29: .z.ph response is a string (type 10h)
assert["echo response is type 10h"; 10h=type echoResp]

/ .
/ Iteration 3 tests — router
/ .

/ Test 30: GET / returns HTTP/1.1 200
rootResp:.z.ph(""; (enlist`Host)!enlist"localhost")
assert["GET / returns HTTP/1.1 200"; strContains[rootResp;"HTTP/1.1 200"]]

/ Test 31: GET / response contains <!DOCTYPE html>
assert["GET / response contains <!DOCTYPE html>"; strContains[rootResp;"<!DOCTYPE html>"]]

/ Test 32: GET / response contains id='process-info'
assert["GET / response contains id='process-info'"; strContains[rootResp;"id='process-info'"]]

/ Test 33: GET / response contains id='object-browser'
assert["GET / response contains id='object-browser'"; strContains[rootResp;"id='object-browser'"]]

/ Test 34: GET /no-such-page returns HTTP/1.1 404
notFoundResp:.z.ph("/no-such-page"; (enlist`Host)!enlist"localhost")
assert["GET /no-such-page returns HTTP/1.1 404"; strContains[notFoundResp;"HTTP/1.1 404"]]

/ Test 35: GET /no-such-page response contains id='not-found'
assert["GET /no-such-page response contains id='not-found'"; strContains[notFoundResp;"id='not-found'"]]

/ .
/ Iteration 3 tests — HTML function unit tests
/ .

/ Test 36: htmlPage includes the given title in <title> tag
pgOut:htmlPage["Test";"<p>body</p>"]
assert["htmlPage includes <title>Test</title>"; strContains[pgOut;"<title>Test</title>"]]

/ Test 37: htmlPage includes the body content
assert["htmlPage includes body content <p>body</p>"; strContains[pgOut;"<p>body</p>"]]

/ Test 38: htmlProcessInfo contains id='process-info'
piOut:htmlProcessInfo[]
assert["htmlProcessInfo contains id='process-info'"; strContains[piOut;"id='process-info'"]]

/ Test 39: htmlProcessInfo returns a string (type 10h)
assert["htmlProcessInfo is type 10h"; 10h=type piOut]

/ Test 40: htmlObjectBrowser contains id='object-browser'
obOut:htmlObjectBrowser[]
assert["htmlObjectBrowser contains id='object-browser'"; strContains[obOut;"id='object-browser'"]]

/ Test 41: htmlObjectBrowser returns a string (type 10h)
assert["htmlObjectBrowser is type 10h"; 10h=type obOut]

/ Test 42: html404 contains id='not-found'
nfOut:html404["/x"]
assert["html404 contains id='not-found'"; strContains[nfOut;"id='not-found'"]]

/ Test 43: html404 includes the path argument in the output
assert["html404 includes the path /x"; strContains[nfOut;"/x"]]

/ .
/ Iteration 4 tests — static file server
/ .

/ Test 44: mimeType css returns text/css
assert["mimeType css returns text/css"; strContains[mimeType "css";"text/css"]]

/ Test 45: mimeType js returns application/javascript
assert["mimeType js returns application/javascript"; strContains[mimeType "js";"application/javascript"]]

/ Test 46: handleStatic serves style.css with 200
staticResp:.z.ph("/static/style.css"; (enlist`Host)!enlist"localhost")
assert["GET /static/style.css returns 200"; strContains[staticResp;"HTTP/1.1 200"]]

/ Test 47: handleStatic style.css response contains CSS content
assert["GET /static/style.css body contains css"; strContains[staticResp;"body{"]]

/ Test 48: handleStatic style.css Content-Type is text/css
assert["GET /static/style.css Content-Type is text/css"; strContains[staticResp;"text/css"]]

/ Test 49: handleStatic missing file returns 404
missingResp:.z.ph("/static/no-such-file.css"; (enlist`Host)!enlist"localhost")
assert["GET /static/missing returns 404"; strContains[missingResp;"HTTP/1.1 404"]]

/ Test 50: handleStatic path traversal rejected
traversalResp:.z.ph("/static/../src/zph.q"; (enlist`Host)!enlist"localhost")
assert["path traversal rejected (400 or 404)"; strContains[traversalResp;"HTTP/1.1 400"] or strContains[traversalResp;"HTTP/1.1 404"]]

/ Test 51: GET / still works after htmlPage change
rootResp2:.z.ph(""; (enlist`Host)!enlist"localhost")
assert["GET / still returns 200 after htmlPage change"; strContains[rootResp2;"HTTP/1.1 200"]]

/ Test 52: GET / head contains link to stylesheet
assert["GET / head links to /static/style.css"; strContains[rootResp2;"/static/style.css"]]

/ Test 53: GET / head does NOT contain inline <style> tag
assert["GET / head has no inline <style> tag"; not strContains[rootResp2;"<style>"]]

/ .
/ Object browser variable display tests
/ .

/ set a known test variable, then re-render
testVarZPH:999
obOut3:htmlObjectBrowser[]
assert["htmlObjectBrowser shows plain variable name"; strContains[obOut3;"testVarZPH"]]
assert["htmlObjectBrowser shows variable type number"; strContains[obOut3;"-7"]]

/ .
/ Iteration 5 tests — POST handler + JSON layer
/ .

/ Test 56: POST with ping action returns HTTP 200
pingResp:.z.pp enlist "{\"action\":\"ping\"}"
assert["POST ping returns HTTP 200"; strContains[pingResp;"HTTP/1.1 200"]]

/ Test 57: ping response body contains "status"
assert["POST ping body contains status"; strContains[pingResp;"status"]]

/ Test 58: ping response body contains "ok"
assert["POST ping body contains ok"; strContains[pingResp;"ok"]]

/ Test 59: ping response has Content-Type application/json
assert["POST ping Content-Type is application/json"; strContains[pingResp;"application/json"]]

/ Test 60: ping response has Access-Control-Allow-Origin header
assert["POST ping has CORS header"; strContains[pingResp;"Access-Control-Allow-Origin"]]

/ Test 61: malformed JSON returns HTTP 400
badJsonResp:.z.pp enlist "not valid json"
assert["malformed JSON returns HTTP 400"; strContains[badJsonResp;"HTTP/1.1 400"]]

/ Test 62: malformed JSON response contains "error"
assert["malformed JSON body contains error"; strContains[badJsonResp;"error"]]

/ Test 63: unknown action returns HTTP 400
unknownResp:.z.pp enlist "{\"action\":\"no_such_action\"}"
assert["unknown action returns HTTP 400"; strContains[unknownResp;"HTTP/1.1 400"]]

/ Test 64: unknown action response contains "error"
assert["unknown action body contains error"; strContains[unknownResp;"error"]]

/ Summary
-1 "";
-1 "Results: ",(string pass)," passed, ",(string fail)," failed";
if[fail>0; exit 1]
