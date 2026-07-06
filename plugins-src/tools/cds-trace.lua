--[[ cds-trace.lua  (w2)  — Cheat Engine autorun 기반 "find what accesses" 자동화
  CE는 커널/VEH 디버거로 BP를 걸어 이 게임을 안 죽인다(유저모드 HW-BP는 크래시 → find-what-accesses.py 폐기).
  이 스크립트는 CE autorun 폴더에 두면 CE 시작 시 자동 실행되며, job 파일이 있을 때만 동작한다.

  job 파일:  %TEMP%\cds_trace_job.txt   (줄단위)
     1: addr_hex      (예: 5A4E40)
     2: size          (1/2/4/8)
     3: trigger       (access | write)
     4: seconds       (수집 시간)
     5: outfile       (결과 경로)
     6: proc          (프로세스명, 기본 cds_95.exe)
  결과:  outfile 에 접근 명령 목록(EIP, 카운트, 접근명령 주소+디스어셈) 기록.
         마지막 줄 "=== DONE ===" 로 완료 표시(하네스가 이걸 기다림).

  하네스: ce-trace.ps1  (job 파일 쓰고 CE 실행, 결과 대기/출력, CE 종료)
]]--

local JOB = os.getenv("TEMP") .. "\\cds_trace_job.txt"

local function readJob()
  local jf = io.open(JOB, "r"); if not jf then return nil end
  local t = {}; for line in jf:lines() do t[#t+1] = (line:gsub("%s+$", "")) end
  jf:close()
  if #t < 5 then return nil end
  return { addr = tonumber(t[1], 16), size = tonumber(t[2]) or 4, trigger = t[3],
           seconds = tonumber(t[4]) or 20, outfile = t[5], proc = t[6] or "cds_95.exe" }
end

local job = readJob()
if not job or not job.addr then return end   -- job 없으면 일반 CE 사용 방해 없이 종료
os.remove(JOB)                               -- 소비(중복 실행 방지)

local function run()
  local out = io.open(job.outfile, "w")
  local function log(s) out:write(s .. "\n"); out:flush() end
  log(string.format("[cds-trace] addr=%X size=%d trigger=%s seconds=%d proc=%s",
      job.addr, job.size, job.trigger, job.seconds, job.proc))

  local ok, err = pcall(function()
    openProcess(job.proc)
    local pid = getOpenedProcessID()
    if not pid or pid == 0 then error("openProcess 실패(게임 실행중?)") end
    log("opened pid=" .. pid)

    local TEXT_LO, TEXT_HI = 0x401000, 0x4C3000   -- .text 범위(호출자 return address 필터)
    local hits = {}
    function debugger_onBreakpoint()
      local eip = EIP or RIP or 0
      local r = hits[eip]
      if not r then r = { count = 0, EAX=EAX, ECX=ECX, EDX=EDX, EBX=EBX, ESI=ESI, EDI=EDI, EBP=EBP, ESP=ESP, callers={} }; hits[eip] = r end
      r.count = r.count + 1
      -- w6: 호출자 캡처 — 스택 [ESP..ESP+0x40]에서 .text 범위 값(=return address 후보) 수집
      if ESP then
        for off = 0, 0x40, 4 do
          local okv, v = pcall(readInteger, ESP + off)
          if okv and v and v >= TEXT_LO and v < TEXT_HI then
            r.callers[v] = (r.callers[v] or 0) + 1
          end
        end
      end
      return 1   -- 자동 계속(디버거 UI 안 띄움)
    end

    local trig = (job.trigger == "write") and bptWrite or bptAccess
    debug_setBreakpoint(job.addr, job.size, trig, bpmDebugRegister)
    log("armed. collecting " .. job.seconds .. "s ... (지금 대상 접근을 유발하세요)")

    local t = createTimer(nil, false)
    t.Interval = job.seconds * 1000
    t.OnTimer = function(timer)
      timer.destroy()
      pcall(function() debug_removeBreakpoint(job.addr) end)
      local n = 0
      for eip, r in pairs(hits) do
        n = n + 1
        local instr = eip
        pcall(function() instr = getPreviousOpcode(eip) end)   -- 데이터BP는 EIP가 접근명령 다음
        local dis = ""
        pcall(function() dis = disassemble(instr) end)
        log(string.format("HIT eip=%X count=%d  @%X: %s", eip, r.count, instr, dis))
        log(string.format("     EAX=%X ECX=%X EDX=%X EBX=%X ESI=%X EDI=%X EBP=%X ESP=%X",
            r.EAX or 0, r.ECX or 0, r.EDX or 0, r.EBX or 0, r.ESI or 0, r.EDI or 0, r.EBP or 0, r.ESP or 0))
        -- w6: 호출자(스택 return address) 후보를 빈도순으로 — 낮은 빈도 = 로드 시점 등 희귀 호출자
        local cl = {}
        for a, c in pairs(r.callers or {}) do cl[#cl+1] = { a = a, c = c } end
        table.sort(cl, function(x, y) return x.c > y.c end)
        local s = ""
        for i = 1, math.min(8, #cl) do s = s .. string.format(" %X(x%d)", cl[i].a, cl[i].c) end
        if s ~= "" then log("     callers:" .. s) end
      end
      log("total_instr=" .. n)
      log("=== DONE ===")
      out:close()
      -- CE 정상 종료(디버거 클린 detach → 게임 안 죽음). 강제 kill 하면 kill-on-exit로 게임까지 죽음.
      local ct = createTimer(nil, false)
      ct.Interval = 400
      ct.OnTimer = function(x) x.destroy(); pcall(function() closeCE() end) end
      ct.Enabled = true
    end
    t.Enabled = true
  end)

  if not ok then
    log("ERROR: " .. tostring(err)); log("=== DONE ==="); out:close()
    local ct = createTimer(nil, false); ct.Interval = 400
    ct.OnTimer = function(x) x.destroy(); pcall(function() closeCE() end) end; ct.Enabled = true
  end
end

-- CE 완전 초기화 후 실행(autorun 시점이 이르면 디버거 미준비일 수 있음)
local boot = createTimer(nil, false)
boot.Interval = 1500
boot.OnTimer = function(tm) tm.destroy(); local ok,e = pcall(run); if not ok then print("cds-trace run err: "..tostring(e)) end end
boot.Enabled = true
