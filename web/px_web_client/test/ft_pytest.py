# ft_cdp_test 的 python 等价版(websocket-client):
# 列目录/上传/下载/落盘校验,另注入 window.confirm=()=>true 自动接管被占用的连接
# (node 内置 WebSocket 在本机偶发卡在 ws 握手,故用 python 驱动)
import hashlib
import json
import os
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request

import websocket

CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
CDP_PORT = 9226
PAGE_URL = 'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=ft1&pwd_md5=81dc9bdb52d04dc20036dbd8313ed055'
UPLOAD_DIR = 'C:/Users/Public'
UPLOAD_NAME = f'ft_web_test_{int(time.time() * 1000)}.txt'

msg_id = 0
ws = None

def send(method, params=None):
    global msg_id
    msg_id += 1
    ws.send(json.dumps({'id': msg_id, 'method': method, 'params': params or {}}))
    return msg_id

def wait_result(want_id, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = json.loads(ws.recv())
        if msg.get('method') == 'Runtime.consoleAPICalled':
            args = [str(a.get('value', a.get('description', ''))) for a in msg['params']['args']]
            line = ' '.join(args)
            # 只打印关键日志,避免刷屏
            if any(k in line for k in ('失败', '错误', 'error', 'onopen', 'onclose', 'connected', 'connectionState')):
                print(f"  [page] {line}")
        elif msg.get('method') == 'Runtime.exceptionThrown':
            d = msg['params']['exceptionDetails']
            print('  [page exception]', (d.get('exception') or {}).get('description', d.get('text')))
        if msg.get('id') == want_id:
            return msg
    raise TimeoutError(f'CDP 响应超时 id={want_id}')

def evaluate(expr, timeout=30):
    rid = send('Runtime.evaluate', {'expression': expr, 'awaitPromise': True, 'returnByValue': True})
    msg = wait_result(rid, timeout)
    r = msg.get('result', {})
    if 'exceptionDetails' in r:
        raise RuntimeError(f"页面内执行出错: {(r['exceptionDetails'].get('exception') or {}).get('description')}")
    return r.get('result', {}).get('value')

def assert_ok(cond, msg):
    if not cond:
        raise AssertionError(f'断言失败: {msg}')
    print(f'  OK: {msg}')

def main():
    global ws
    user_data_dir = tempfile.mkdtemp(prefix='ft_pytest_')
    chrome = subprocess.Popen([
        CHROME, '--headless=new', f'--remote-debugging-port={CDP_PORT}', '--remote-allow-origins=*',
        f'--user-data-dir={user_data_dir}', '--no-first-run', '--disable-gpu', 'about:blank',
    ])
    try:
        version = None
        for _ in range(40):
            try:
                version = json.load(urllib.request.urlopen(f'http://127.0.0.1:{CDP_PORT}/json/version', timeout=2))
                break
            except Exception:
                time.sleep(0.5)
        if not version:
            raise RuntimeError('CDP 端口未就绪')
        print('Chrome:', version['Browser'])

        req = urllib.request.Request(f'http://127.0.0.1:{CDP_PORT}/json/new?about:blank', method='PUT')
        target = json.load(urllib.request.urlopen(req, timeout=10))
        ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=20, suppress_origin=True)
        ws.settimeout(20)

        wait_result(send('Runtime.enable'))
        wait_result(send('Page.enable'))
        # 自动接管:连接被占用(704)时页面会 window.confirm 询问,headless 下确认框会卡死渲染器
        wait_result(send('Page.addScriptToEvaluateOnNewDocument', {
            'source': 'window.confirm = () => true',
        }))
        wait_result(send('Page.navigate', {'url': PAGE_URL}))

        print('\n[1] 等待页面连接 + ft_data_channel 就绪 ...')
        deadline = time.time() + 90
        while time.time() < deadline:
            if evaluate('!!(window.__ft && window.__ft.ready())'):
                break
            time.sleep(2)
        else:
            status = evaluate('window.__conn ? window.__conn.status() : "(no __conn)"')
            print('  typeof __ft =', evaluate('typeof window.__ft'))
            print('  __ft.ready() =', evaluate('window.__ft ? window.__ft.ready() : "(none)"'))
            print('  最近日志:')
            for line in evaluate('document.querySelectorAll(".log-line").length > 0 ? Array.from(document.querySelectorAll(".log-line")).slice(-15).map(e => e.textContent) : []') or []:
                print('   ', line)
            raise RuntimeError(f'等待超时: window.__ft.ready(), status={status}')
        print('  OK: ft 通道就绪')

        print('\n[2] 列盘符 listDir("/") ...')
        disks = evaluate('window.__ft.listDir("/")')
        print('  盘符:', ', '.join(f['path'] for f in disks['files']))
        assert_ok(len(disks['files']) > 0, '返回盘符列表非空')

        print(f'\n[3] 列真实目录 listDir("{UPLOAD_DIR}") ...')
        dir_list = evaluate(f'window.__ft.listDir({json.dumps(UPLOAD_DIR)})')
        print(f"  条目数: {len(dir_list['files'])},前 5 个:",
              ', '.join(f"{'F' if f['type'] == 2 else 'D'}:{f['name']}" for f in dir_list['files'][:5]))
        assert_ok(len(dir_list['files']) > 0, '目录内容非空')

        print('\n[4] 上传文本文件 ...')
        content = ('GoDesk web_client ft upload test\n'
                   f'时间戳: {time.strftime("%Y-%m-%dT%H:%M:%S")}\n'
                   f'随机: {time.time()}\n中文内容校验: 远程桌面文件传输\n')
        expected_hash = hashlib.sha256(content.encode('utf-8')).hexdigest()
        up = evaluate(f'window.__ft.uploadText({json.dumps(UPLOAD_NAME)}, {json.dumps(UPLOAD_DIR)}, {json.dumps(content)})', timeout=60)
        print('  上传结果:', json.dumps(up))
        assert_ok(up['sha256'] == expected_hash, f'本端 sha256 一致 ({expected_hash[:16]}...)')
        assert_ok(up['taskId'].startswith('up-'), f"render 确认上传成功 (taskId={up['taskId']})")

        print('\n[5] 在 render 机器上校验落盘文件 ...')
        remote_path = f'{UPLOAD_DIR}/{UPLOAD_NAME}'
        with open(remote_path, 'rb') as f:
            disk_bytes = f.read()
        disk_hash = hashlib.sha256(disk_bytes).hexdigest()
        assert_ok(disk_hash == expected_hash, f'落盘文件 sha256 一致 ({disk_hash[:16]}...)')
        assert_ok(disk_bytes.decode('utf-8') == content, '落盘文件内容逐字节一致')

        print('\n[6] 下载该文件回来 ...')
        down = evaluate(f'window.__ft.download({json.dumps(remote_path)})', timeout=60)
        print('  下载结果:', json.dumps({k: v for k, v in down.items() if k != 'data'}))
        assert_ok(down['sha256'] == expected_hash, f"下载文件 sha256 一致 ({down['sha256'][:16]}...)")
        assert_ok(down['size'] == len(content.encode('utf-8')), f"大小一致 ({down['size']} bytes)")

        print('\n[7] 清理远端测试文件 ...')
        os.remove(remote_path)

        print('\n全部通过 ✔')
    finally:
        chrome.kill()

main()
