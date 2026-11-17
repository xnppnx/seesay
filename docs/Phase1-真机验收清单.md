# SeeSay Phase 1 真机验收清单

目标：在开发机（CardputerZero-4758f186）上验证纯语音闭环。
软件已经写完，但**本机没有编译工具链、也没有模型/声卡**，所以最后几步必须在真机上做。

---

## 第 0 步：拿到 deb 安装包（在电脑上）

代码改动需要云端编译成 deb：

1. 把 `seesay/` 工程初始化为 git 仓库并推到 `https://github.com/xnppnx/seesay`（账号 xnppnx）。
   - 新工程还没有 `.git`，这是故意的（存档工程 cz-app 的 git 没有复制过来）。
   - 首次推送：`git init && git add -A && git commit -m "Phase 1 voice loop" && git remote add origin <url> && git push -u origin main`
2. 推送触发 GitHub Actions：下载依赖 → 交叉编译 → 打包 deb。
3. 从 Actions 的 artifact 下载 `SeeSay_0.1.0_rev1_arm64.deb`。

> ⚠️ 第一次 CI 很可能因为依赖下载脚本里的某个文件名/版本号对不上而失败。
> 看 Actions 日志的报错，改 `tools/fetch-offline-payload.sh` 里对应的名字，再 push 一次。
> 常见于 espeak-ng deb 的精确版本号、cactus wheel 的 ABI 标签。

## 第 1 步：安装（在真机上）

```sh
# 传包（Git Bash 下必须加这个，否则 /data/local/tmp 会被路径转换）
export MSYS_NO_PATHCONV=1
adb push <deb路径> /data/local/tmp/

# 真机安装
adb shell dpkg -i /data/local/tmp/SeeSay_0.1.0_rev1_arm64.deb

# 首次需要装离线依赖包（payload 里自带，只在第一次装，之后不联网）
adb shell "sudo /usr/share/seesay/payload/install.sh"
```

安装后确认：
- `adb shell "dpkg -l | grep -i seesay"` 能看到包
- 应用图标出现在 APPLaunch 里（眼睛+声波的那个）

## 第 2 步：开机自启与身份

```sh
adb shell "sudo systemctl status seesay.service"
adb shell "ps aux | grep seesay"
```

确认：进程以 **seesay 用户**运行（不是 root）——这是上架红线。
如果服务没起来：`adb shell "sudo systemctl restart seesay.service"`。

## 第 3 步：断网验证（核心）

```sh
# 关掉网络（确保"完全离线"承诺）
adb shell "sudo ip link set wlan0 down"   # 或飞行模式
```

然后**不看屏幕**操作，全程靠听：

| 动作 | 应该听到 | 通过? |
|---|---|---|
| 启动应用 | "SeeSay ready. Press the button to take a photo." | ☐ |
| 按 **空格**（对着桌面键盘） | "Taking photo." → "Analyzing the scene." → "I can see a desk, a keyboard..." | ☐ |
| 按 **回车** 或 **ESC 短按** | 把刚才那句重念一遍 | ☐ |
| 按 **空格**（对着英文菜单） | "...Text reads: ..." | ☐ |
| 按 **空格**（对着中文纸） | 中文念出来（机械腔可接受） | ☐ |
| 对着空白/反光乱按 | "I am not sure. Please try again."（不乱说） | ☐ |
| 按 **fn+H** | 播报/显示按键帮助 | ☐ |
| **长按 ESC** | 回到系统 HOME（应用不自己退出） | ☐ |

## 第 4 步：听回核对内容

用语音识别工具把播报转成文字，对照实际物体/文字是否正确。
（之前用 AudioTranscribe 听回验证过 espeak 中英文可懂。）

记录几张标准测试图的结果：

| 测试图 | 期望 | 实际听到 | 是否通过 |
|---|---|---|---|
| 桌面（杯子/键盘/笔记本） | 认出常见物体 | | ☐ |
| 英文菜单 | 读出文字 | | ☐ |
| 中文纸 | 中文念出 | | ☐ |
| 反光笔记本（易误认） | 说"不确定"或不乱报 car/person | | ☐ |

## 第 5 步：出问题时的定位入口

```sh
# pipeline 的错误日志
adb shell "cat /tmp/seesay_pipeline_err.txt"

# 每一步状态（SHOOTING / ANALYZING / DONE / UNSURE / ERROR）
adb shell "cat /tmp/seesay_status.txt"

# 最终结果 JSON（物体、文字、播报文案都在里面）
adb shell "cat /tmp/seesay_result.json"

# 手动单测某个引擎（不跑整个 App）
adb shell "python3 /usr/share/seesay/runtime/seesay_describe.py /tmp/seesay_capture.jpg"
adb shell "python3 /usr/share/seesay/runtime/seesay_readtext.py /tmp/seesay_capture.jpg"
adb shell "python3 /usr/share/seesay/runtime/seesay_speak.py 'Testing voice'"
```

## 已知限制（Phase 1 接受，不算 bug）

- 一次完整识别约 15-20 秒（慢，但语音提示填满等待）。
- espeak 中文是机械腔，能听懂即可。
- Mirage 认场所可能出错，所以场景（setting）**不进语音**，只念物体+文字。
- 屏幕只是开发调试用，坏不影响语音流程。
