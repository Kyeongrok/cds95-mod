--[[ cds-disasm.lua  — CE autorun 기반 주소별 디스어셈블 덤프
  ce-trace.lua 와 같은 방식(CE autorun + job 파일 + closeCE 자기종료).
  find-what-accesses 로 잡은 호출자 주변 코드를 확인해 "어느 호출자가 스프라이트를
  그리는지" 같은 판별에 사용.

  job 파일:  %TEMP%\cds_disasm_job.txt   (줄단위)
     1: addrs        (콤마구분 hex, 예: 48A853,48EF5A,48D0F4)
     2: count        (각 주소에서 앞으로 디스어셈블할 명령 수)
     3: outfile      (결과 경로)
     4: proc         (프로세스명, 기본 cds_95.exe)
  결과:  outfile 에 각 주소부터 forward count개 명령. 마지막 "=== DONE ===".
]]--

local JOB = os.getenv("TEMP") .. "\\cds_disasm_job.txt"
local jf = io.open(JOB, "r"); if not jf then return end
local lines = {}; for l in jf:lines() do lines[#lines+1] = (l:gsub("%s+$", "")) end
jf:close()
if #lines < 3 then return end
os.remove(JOB)

local addrs = {}
for a in lines[1]:gmatch("[^,]+") do addrs[#addrs+1] = tonumber(a, 16) end
local count = tonumber(lines[2]) or 10
local outfile = lines[3]
local proc = lines[4] or "cds_95.exe"

local function run()
  local out = io.open(outfile, "w")
  local function log(s) out:write(s .. "\n"); out:flush() end
  local ok, err = pcall(function()
    openProcess(proc)
    local pid = getOpenedProcessID()
    if not pid or pid == 0 then error("openProcess 실패(게임 실행중?)") end
    log("opened pid=" .. pid)
    for _, a in ipairs(addrs) do
      log(string.format("=== @%X  forward %d ===", a, count))
      local p = a
      for i = 1, count do
        local dis = ""
        pcall(function() dis = disassemble(p) end)
        log("  " .. dis)
        local sz = 1
        pcall(function() sz = getInstructionSize(p) end)
        if not sz or sz < 1 then sz = 1 end
        p = p + sz
      end
    end
  end)
  if not ok then log("ERROR: " .. tostring(err)) end
  log("=== DONE ===")
  out:close()
  local ct = createTimer(nil, false); ct.Interval = 400
  ct.OnTimer = function(x) x.destroy(); pcall(function() closeCE() end) end; ct.Enabled = true
end

local boot = createTimer(nil, false)
boot.Interval = 1500
boot.OnTimer = function(tm) tm.destroy(); local ok, e = pcall(run); if not ok then print("cds-disasm err: " .. tostring(e)) end end
boot.Enabled = true
