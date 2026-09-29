# Live Editor for OBS

OBS Studio 안에서 치지직 방송 제목, 카테고리, 태그를 조회하고 변경하는 Windows용 도크 플러그인입니다.

## 다운로드

일반 사용자는 [배포 저장소](https://github.com/TereBin/obs-live-editor-releases)에서 최신 Windows 설치 파일을 내려받으세요. 이 저장소는 소스 코드와 개발 문서를 제공합니다.

## 요구 사항

- Windows 10 또는 Windows 11 64비트
- OBS Studio 64비트
- 치지직 스트리머 계정

## 설치

1. OBS Studio를 종료합니다.
2. 배포 저장소에서 `obs-live-editor-<버전>-windows-x64-setup.exe`를 내려받습니다.
3. 설치 파일을 실행하고 관리자 권한 요청을 승인합니다.
4. OBS Studio를 실행합니다.
5. `도크` 메뉴에서 `라이브 정보 편집`을 엽니다.

현재 설치 파일은 코드 서명이 되어 있지 않아 Windows SmartScreen 경고가 나타날 수 있습니다. 배포 저장소에 공개된 SHA-256 체크섬과 파일 해시를 비교할 수 있습니다.

## 사용

1. 도크의 `로그인` 버튼을 누릅니다.
2. 브라우저에서 치지직 로그인을 완료한 뒤 OBS로 돌아옵니다.
3. 제목을 수정하거나 카테고리를 검색해 결과에서 선택합니다.
4. 태그는 쉼표로 구분해 입력합니다.
5. `적용`을 누르면 치지직 방송 설정에 반영됩니다.

`새로고침`은 치지직의 현재 설정을 다시 불러옵니다. 카테고리 오른쪽의 초기화 버튼을 누르고 적용하면 카테고리가 제거됩니다. 카테고리는 두 글자 이상 입력하면 검색되며 한글 IME 조합이 끝난 뒤 350ms 후 요청됩니다.

## 지원 기능

- 라이브 기본 제목 조회 및 변경
- 카테고리 검색, 선택 및 제거
- 태그 조회, 변경 및 전체 제거
- OAuth 로그인과 Access Token 자동 갱신
- Windows DPAPI를 이용한 로컬 토큰 암호화
- Windows Schannel 기반 HTTPS 통신
- 로그아웃 시 치지직 토큰 폐기 요청
- GitHub Releases 기반 업데이트 확인과 중요도별 알림
- 최소 지원 버전 미만 클라이언트에 대한 필수 업데이트 안내

## 보안과 개인정보

- 사용자의 Access Token과 Refresh Token은 Windows 사용자 계정에 묶인 DPAPI로 암호화되어 로컬에 저장됩니다.
- Worker는 토큰을 데이터베이스나 파일에 저장하지 않습니다.
- 방송 설정 API 요청은 OBS에서 치지직 Open API로 직접 전송됩니다.
- 토큰 발급·갱신·폐기와 카테고리 검색만 Worker를 통과합니다.
- Client Secret, 실제 토큰, `.dev.vars`, `.wrangler`, `credentials.bin`은 저장소에 커밋하면 안 됩니다.

## 소스 빌드

필요한 도구:

- Visual Studio 2022와 `Desktop development with C++` 워크로드
- CMake 3.28 이상
- Git

Worker 주소를 지정해 구성하고 빌드합니다.

```powershell
cmake --preset windows-x64 -DCHZZK_BROKER_URL=https://YOUR-WORKER.workers.dev
cmake --build --preset windows-x64
cmake --install build_x64 --config RelWithDebInfo --prefix release/RelWithDebInfo
```

## Worker 배포

직접 빌드한 플러그인을 배포하려면 치지직 개발자 센터에서 애플리케이션을 등록하고 Cloudflare Worker를 배포해야 합니다.

1. 로그인 리디렉션 URL을 `http://127.0.0.1:20132/callback`으로 설정합니다.
2. API Scope에서 방송 설정 조회와 방송 설정 변경을 활성화합니다.
3. `worker/wrangler.jsonc`의 Worker 이름과 필요 설정을 확인합니다.
4. Worker를 배포하고 Secret을 등록합니다.

```powershell
cd worker
npm install
npx wrangler secret put CHZZK_CLIENT_ID
npx wrangler secret put CHZZK_CLIENT_SECRET
npx wrangler secret put CHZZK_REDIRECT_URI
npx wrangler secret put STATE_SECRET
npx wrangler deploy
```

`CHZZK_REDIRECT_URI` 값은 `http://127.0.0.1:20132/callback`입니다. `STATE_SECRET`에는 충분히 긴 무작위 문자열을 사용하세요. Windows 설정 도우미를 사용할 때는 배포된 주소를 명시합니다.

```powershell
./worker/Configure-Secrets.ps1 -WorkerUrl https://YOUR-WORKER.workers.dev
```

Worker의 `wrangler.jsonc`에는 다음 공개 업데이트 정책이 있습니다.

- `MINIMUM_CLIENT_VERSION`: Worker가 허용하는 최소 버전
- `LATEST_CLIENT_VERSION`: 사용자에게 안내할 최신 버전
- `CLIENT_UPDATE_LEVEL`: `optional`, `recommended`, `required`, `security` 중 하나
- `CLIENT_UPDATE_MESSAGE`: 업데이트 안내 문구
- `CLIENT_RELEASE_URL`: 다운로드 페이지

호환성이 깨지는 변경을 배포할 때는 새 설치 파일과 `update-manifest.json`을 먼저 공개한 뒤 Worker의 최소 버전을 올려 배포하세요. 최소 버전 미만 요청에는 HTTP `426 Upgrade Required`가 반환됩니다.

## 설치 프로그램 생성

Inno Setup 6을 설치한 뒤 다음 명령을 실행합니다.

```powershell
winget install --id JRSoftware.InnoSetup
./installer/Build-Installer.ps1 -Configuration RelWithDebInfo
```

결과 파일은 `release/obs-live-editor-<버전>-windows-x64-setup.exe`입니다. GitHub Actions로 배포 파일을 만들려면 저장소 변수 `CHZZK_BROKER_URL`을 설정해야 합니다. Client Secret은 GitHub Actions에 필요하지 않으며 등록하지 않는 것을 권장합니다.

## 테스트

Worker 테스트는 Node.js 22 이상에서 실행할 수 있습니다.

```powershell
node --test worker/test/oauth.test.mjs
```

## 라이선스

GPL-2.0 라이선스로 배포됩니다. 자세한 내용은 [LICENSE](LICENSE)를 확인하세요.
