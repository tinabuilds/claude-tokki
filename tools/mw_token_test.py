"""Does a Bambu cloud login token read MakerWorld private stats from api.bambulab.com?
Run it yourself; it prints numbers only, never the token, and saves nothing."""
import getpass, json, urllib.request, urllib.error

API = "https://api.bambulab.com"

def call(path, body=None, token=None):
    req = urllib.request.Request(API + path, data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Content-Type": "application/json", "User-Agent": "tokki-test"})
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, (e.read() or b"")[:200].decode(errors="replace")

email = input("Bambu email: ").strip()
pw = getpass.getpass("Password (Google/Apple sign-in: just press Enter): ")
st, r = (call("/v1/user-service/user/login", {"account": email, "password": pw}) if pw
         else (200, {"loginType": "verifyCode"}))
if st == 200 and isinstance(r, dict) and r.get("loginType") == "verifyCode":
    call("/v1/user-service/user/sendemail/code", {"email": email, "type": "codeLogin"})
    st, r = call("/v1/user-service/user/login", {"account": email, "code": input("Code from email: ").strip()})
token = r.get("accessToken") if isinstance(r, dict) else None
if not token:
    raise SystemExit(f"login failed: {st} {r if not isinstance(r, dict) else r.get('loginType') or r.get('message')}")
print(f"login ok, token expires in {r.get('expiresIn', '?')} s")

st, p = call("/v1/user-service/my/profile", token=token)
print("profile", st, {k: p.get(k) for k in ("name", "fanCount", "point", "likeCount", "downloadCount")} if st == 200 else p)
st, m = call("/v1/user-service/my/message/count", token=token)
print("messages", st, {k: m.get(k) for k in ("commentCount", "unreadTotal")} if st == 200 else m)
st, b = call("/v1/point-service/point-bill/my?offset=0&limit=1&filter=all", token=token)
print("point bill", st, b.get("totalIncome") if st == 200 else b)
