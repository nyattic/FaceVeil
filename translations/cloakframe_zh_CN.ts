<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>cloakframe::MainWindow</name>
    <message>
        <location filename="../src/MainWindow.cpp" line="332"/>
        <source>Downloading model…</source>
        <translation>正在下载模型…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="333"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="361"/>
        <location filename="../src/MainWindow.cpp" line="373"/>
        <location filename="../src/MainWindow.cpp" line="385"/>
        <location filename="../src/MainWindow.cpp" line="394"/>
        <source>Download Failed</source>
        <translation>下载失败</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="362"/>
        <source>Could not download the model.

%1</source>
        <translation>无法下载模型。

%1</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="374"/>
        <source>The downloaded model failed its integrity check and was discarded.</source>
        <translation>下载的模型未通过完整性检查，已被丢弃。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="398"/>
        <source>The download was much larger than expected and was stopped.</source>
        <translation>下载量远超预期，已停止下载。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="386"/>
        <location filename="../src/MainWindow.cpp" line="395"/>
        <source>Could not save the model file.</source>
        <translation>无法保存模型文件。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="406"/>
        <source>Download Model</source>
        <translation>下载模型</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="407"/>
        <source>The %1 model isn&apos;t on this computer yet.

CloakFrame can download it once (%2 MB) from Hugging Face. The model is provided by InsightFace for non-commercial use. Your images are never uploaded.

Download now?</source>
        <translation>%1 模型尚未安装在此电脑上。

CloakFrame 可以从 Hugging Face 下载一次（%2 MB）。该模型由 InsightFace 提供，仅限非商业用途。您的图像绝不会被上传。

立即下载？</translation>
    </message>
    <message>
        <source>The license plate detection model isn&apos;t on this computer yet.

CloakFrame can download it once (%1 MB) from the open-image-models project (MIT-licensed). Your images are never uploaded.

Download now?</source>
        <translation>车牌检测模型尚未安装在此电脑上。

CloakFrame 可以从 open-image-models 项目下载一次（%1 MB，MIT 许可证）。您的图像绝不会被上传。

立即下载？</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="449"/>
        <location filename="../src/MainWindow.cpp" line="455"/>
        <source>Invalid Model</source>
        <translation>模型无效</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="450"/>
        <source>Choose an existing ONNX model file.</source>
        <translation>请选择现有的 ONNX 模型文件。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="456"/>
        <source>The selected model must use the .onnx extension.</source>
        <translation>所选模型必须使用 .onnx 扩展名。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="461"/>
        <source>Model Too Large</source>
        <translation>模型过大</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="462"/>
        <source>The selected ONNX file is larger than 512 MB. Choose a smaller SCRFD model.</source>
        <translation>所选 ONNX 文件超过 512 MB。请选择较小的 SCRFD 模型。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="475"/>
        <source>Load Custom Model</source>
        <translation>加载自定义模型</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="476"/>
        <source>Only load ONNX models from sources you trust.

Model: %1
Size: %2 MB

Continue?</source>
        <translation>请仅加载来源可信的 ONNX 模型。

模型：%1
大小：%2 MB

继续？</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="554"/>
        <source>Local, private redaction of faces and license plates in photos and videos</source>
        <translation>在本地私密遮盖照片和视频中的人脸与车牌</translation>
    </message>
    <message>
        <source>Remove Selected</source>
        <translation>移除所选项</translation>
    </message>
    <message>
        <source>Clear All</source>
        <translation>全部清除</translation>
    </message>
    <message numerus="yes">
        <source>Ignored %n unsupported file(s).</source>
        <translation><numerusform>已忽略 %n 个不支持的文件。</numerusform></translation>
    </message>
    <message>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <source>Anonymization style preview</source>
        <translation>匿名化样式预览</translation>
    </message>
    <message>
        <source>Sample of the current anonymization style and block size.</source>
        <translation>当前匿名化样式和马赛克块大小的示例。</translation>
    </message>
    <message>
        <source>Input images and folders</source>
        <translation>输入图像和文件夹</translation>
    </message>
    <message>
        <source>Right-click for options · Delete removes selected items</source>
        <translation>右键单击查看更多选项 · Delete 键移除所选项</translation>
    </message>
    <message>
        <source>Processing progress</source>
        <translation>处理进度</translation>
    </message>
    <message>
        <source>Open Output Folder</source>
        <translation>打开输出文件夹</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="567"/>
        <source>Model</source>
        <translation>模型</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="572"/>
        <source>Choose speed vs. accuracy, or load a custom SCRFD ONNX file.</source>
        <translation>选择速度或准确度，也可加载自定义 SCRFD ONNX 文件。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="582"/>
        <source>Bundled SCRFD model path</source>
        <translation>内置 SCRFD 模型路径</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="584"/>
        <source>Browse…</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="583"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="605"/>
        <source>Inputs</source>
        <translation>输入</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="610"/>
        <source>Drag images, videos, or folders here, or use the buttons below.</source>
        <translation>将图像、视频或文件夹拖到此处，或使用下方按钮。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="620"/>
        <source>Drop images, videos, or folders here</source>
        <translation>将图像、视频或文件夹拖到此处</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="621"/>
        <source>Add Files</source>
        <translation>添加文件</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="623"/>
        <source>Add Folder</source>
        <translation>添加文件夹</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="625"/>
        <source>Clear</source>
        <translation>清除</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="630"/>
        <source>Include subfolders</source>
        <translation>包括子文件夹</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="633"/>
        <source>Review before saving</source>
        <translation>保存前检查</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="637"/>
        <source>Review detections before output:
  • Images: exclude boxes or add missed regions
  • Videos: scrub the track timeline and exclude false tracks</source>
        <translation>输出前检查检测结果：
  • 图像：排除检测框或添加遗漏区域
  • 视频：浏览轨迹时间线并排除错误轨迹</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="666"/>
        <source>Output</source>
        <translation>输出</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="671"/>
        <source>Anonymized copies are written here, preserving folder structure.</source>
        <translation>匿名化副本将写入此处，并保留文件夹结构。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="677"/>
        <source>Choose…</source>
        <translation>选择…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="686"/>
        <source>Preserve selected EXIF metadata</source>
        <translation>保留选定的 EXIF 元数据</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="690"/>
        <source>Off (default): output carries no metadata — GPS, camera, and timestamps are removed.
On: copies selected EXIF fields such as camera, timestamps, and location. Embedded previews, IPTC, XMP, comments, and color profiles are removed. Format and bit depth are preserved at maximum quality.</source>
        <translation>关（默认）：输出不含任何元数据，GPS、相机和时间戳将被移除。
开：仅复制相机、时间戳和位置等选定的 EXIF 字段。嵌入式预览、IPTC、XMP、注释和色彩配置文件将被移除，并以最高质量保留格式和位深度。</translation>
    </message>
    <message>
        <source>Metadata preservation is unavailable in this build. Output metadata will be removed.</source>
        <translation>此构建不支持保留元数据。输出元数据将被移除。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="699"/>
        <source>Advanced Options</source>
        <translation>高级选项</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="708"/>
        <source>Reset to defaults</source>
        <translation>恢复默认设置</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="457"/>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <source>Update available: %1</source>
        <translation>有可用更新：%1</translation>
    </message>
    <message>
        <source>Update Available</source>
        <translation>有可用更新</translation>
    </message>
    <message>
        <source>CloakFrame %1 is available. What's new:</source>
        <translation>CloakFrame %1 已发布。更新内容：</translation>
    </message>
    <message>
        <source>No release notes were provided for this update.</source>
        <translation>此更新未提供发行说明。</translation>
    </message>
    <message>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <source>Later</source>
        <translation>稍后</translation>
    </message>
    <message>
        <source>Faces</source>
        <translation>人脸</translation>
    </message>
    <message>
        <source>License plates</source>
        <translation>车牌</translation>
    </message>
    <message>
        <source>Faces + license plates</source>
        <translation>人脸 + 车牌</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="724"/>
        <source>Tweak detection and mosaic behavior. Defaults work for most photos.</source>
        <translation>调整检测和马赛克效果。默认值适用于大多数照片。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="732"/>
        <source>Mosaic (pixelate)</source>
        <translation>马赛克（像素化）</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="733"/>
        <source>Gaussian blur</source>
        <translation>高斯模糊</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="734"/>
        <source>Solid fill (blackout)</source>
        <translation>纯色填充（涂黑）</translation>
    </message>
    <message>
        <source>Sticker (smiley)</source>
        <translation>贴纸（笑脸）</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="738"/>
        <source>How detected faces are obscured.
Mosaic = pixelation (block size below).
Gaussian blur = strong smoothing scaled to face size.
Solid fill = opaque black box, irreversible.
Sticker = a friendly opaque smiley. Default: Mosaic</source>
        <translation>检测到的人脸遮盖方式。
马赛克 = 像素化（块大小见下方）。
高斯模糊 = 根据人脸大小进行强模糊。
纯色填充 = 不透明黑框，无法还原。
贴纸 = 友好的不透明笑脸。默认：马赛克</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="752"/>
        <source>Minimum confidence to accept a face.
Higher = fewer false positives but may miss small or side-profile faces.
Lower = catches more faces but may blur non-face regions. Default: 0.50</source>
        <translation>接受人脸所需的最低置信度。
越高 = 误报更少，但可能漏掉小脸或侧脸。
越低 = 检出更多人脸，但可能模糊非人脸区域。默认：0.50</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="765"/>
        <source>Non-Maximum Suppression overlap threshold for duplicate boxes.
Lower = more aggressively removes overlapping detections.
Higher = allows more overlap. Default: 0.40</source>
        <translation>用于去除重复框的非极大值抑制重叠阈值。
越低 = 更积极地去除重叠检测。
越高 = 允许更多重叠。默认：0.40</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="776"/>
        <source>Mosaic block size in pixels.
Larger = coarser blocks, harder to un-blur.
Smaller = finer mosaic, higher recovery risk. Default: 14</source>
        <translation>马赛克块大小（像素）。
越大 = 块越粗，更难恢复。
越小 = 马赛克越细，恢复风险越高。默认：14</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="789"/>
        <source>Extra margin around each detected face, as a fraction of its size.
Covers ears, hairline, and chin that the detector may miss.
0.00 = exact box, 0.18 = ~18% larger. Default: 0.18</source>
        <translation>每张已检测人脸周围的额外边距，以人脸大小的比例表示。
用于覆盖检测器可能漏掉的耳朵、发际线和下巴。
0.00 = 原始框，0.18 = 约扩大 18%。默认：0.18</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="801"/>
        <source>Anonymization</source>
        <translation>匿名化</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="803"/>
        <source>Shape</source>
        <translation>形状</translation>
    </message>
    <message>
        <source>Soft edges</source>
        <translation>柔化边缘</translation>
    </message>
    <message>
        <source>Fades the edge of the obscured region into the photo instead of a hard cutoff.
The fade only extends outward, so the detected area stays fully covered. Default: off</source>
        <translation>让遮盖区域边缘淡入照片，而不是生硬截断。
渐变仅向外延伸，因此检测区域仍会被完全覆盖。默认：关</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="747"/>
        <source>Rectangle</source>
        <translation>矩形</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="748"/>
        <source>Rounded (ellipse)</source>
        <translation>圆角（椭圆）</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="752"/>
        <source>Shape of the obscured region.
Rectangle = full padded box.
Rounded = elliptical mask that follows the face and leaves corners untouched. Default: Rectangle</source>
        <translation>遮盖区域的形状。
矩形 = 完整的扩展框。
圆角 = 贴合人脸的椭圆遮罩，四角不遮盖。默认：矩形</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="804"/>
        <source>Score threshold</source>
        <translation>置信度阈值</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="807"/>
        <source>NMS threshold</source>
        <translation>NMS 阈值</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="810"/>
        <source>Mosaic block size</source>
        <translation>马赛克块大小</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="813"/>
        <source>Face padding</source>
        <translation>人脸边距</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="834"/>
        <source>Activity</source>
        <translation>活动</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="856"/>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="866"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="872"/>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="891"/>
        <source>Ready. Drop images, videos, or folders to begin.</source>
        <translation>已就绪。拖入图像、视频或文件夹即可开始。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="963"/>
        <source>Select SCRFD ONNX Model</source>
        <translation>选择 SCRFD ONNX 模型</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="964"/>
        <source>ONNX Models (*.onnx)</source>
        <translation>ONNX 模型 (*.onnx)</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="972"/>
        <location filename="../src/MainWindow.cpp" line="1212"/>
        <source>Custom — %1</source>
        <translation>自定义 — %1</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="981"/>
        <source>Select Images or Videos</source>
        <translation>选择图像或视频</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="984"/>
        <source>Images &amp; Videos (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp *.mp4 *.mov *.m4v)</source>
        <translation>图像与视频 (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp *.mp4 *.mov *.m4v)</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="993"/>
        <source>Select Folder</source>
        <translation>选择文件夹</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1004"/>
        <source>Select Output Folder</source>
        <translation>选择输出文件夹</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1018"/>
        <source>Choose a SCRFD ONNX model first.</source>
        <translation>请先选择 SCRFD ONNX 模型。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1027"/>
        <source>Choose a valid SCRFD ONNX model first.</source>
        <translation>请先选择有效的 SCRFD ONNX 模型。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1030"/>
        <source>Downloading %1…</source>
        <translation>正在下载 %1…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1033"/>
        <source>Model download was cancelled or failed.</source>
        <translation>模型下载已取消或失败。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1037"/>
        <source>Model ready: %1</source>
        <translation>模型已就绪：%1</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1047"/>
        <source>Add at least one image or folder.</source>
        <translation>请至少添加一张图像或一个文件夹。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1053"/>
        <source>Choose an output folder.</source>
        <translation>请选择输出文件夹。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1078"/>
        <source>Refusing to run: output folder is inside input &apos;%1&apos;. Pick a different output folder so originals aren&apos;t overwritten.</source>
        <translation>拒绝运行：输出文件夹位于输入“%1”内。请选择其他输出文件夹，以免覆盖原文件。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1087"/>
        <location filename="../src/MainWindow.cpp" line="1124"/>
        <source>Starting…</source>
        <translation>正在启动…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1132"/>
        <source>Stopping after the current processing step…</source>
        <translation>将在当前处理步骤结束后停止…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1133"/>
        <source>Stopping…</source>
        <translation>正在停止…</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1140"/>
        <source>Cancelled.</source>
        <translation>已取消。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1140"/>
        <source>Finished.</source>
        <translation>已完成。</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1141"/>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1141"/>
        <source>Done</source>
        <translation>完成</translation>
    </message>
    <message>
        <source>Completed with warnings — review the results before sharing.</source>
        <translation>已完成，但有警告——请在分享前检查结果。</translation>
    </message>
    <message>
        <source>Review required</source>
        <translation>需要检查</translation>
    </message>
    <message>
        <source>Review Required</source>
        <translation>需要检查</translation>
    </message>
    <message>
        <source>Processing finished, but some results need attention.

Total: %1
Redacted: %2
Saved without redaction: %3
Copied: %4
Skipped: %5
Failed: %6

Check these results before sharing them.</source>
        <translation>处理已完成，但部分结果需要注意。

总计：%1
已遮盖：%2
未遮盖保存：%3
已复制：%4
已跳过：%5
失败：%6

请在分享前检查这些结果。</translation>
    </message>
    <message>
        <source>Failed — check the log for details.</source>
        <translation>失败——请查看日志了解详情。</translation>
    </message>
    <message>
        <source>Failed — check the log</source>
        <translation>失败——请查看日志</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1361"/>
        <source>Fast  ·  SCRFD 2.5G</source>
        <translation>快速  ·  SCRFD 2.5G</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1362"/>
        <source>Accurate  ·  SCRFD 10G</source>
        <translation>精确  ·  SCRFD 10G</translation>
    </message>
    <message>
        <location filename="../src/MainWindow.cpp" line="1379"/>
        <source>Not downloaded yet — click Download</source>
        <translation>尚未下载——请单击“下载”</translation>
    </message>
</context>
<context>
    <name>cloakframe::ProcessorWorker</name>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="50"/>
        <source>cannot inspect image dimensions</source>
        <translation>无法检查图像尺寸</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="59"/>
        <source>image too large, %1 x %2</source>
        <translation>图像过大，%1 × %2</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="178"/>
        <source>Output name collision: &apos;%1&apos; and &apos;%2&apos; would both write to &apos;%3&apos;</source>
        <translation>输出名称冲突：“%1”和“%2”都将写入“%3”</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="185"/>
        <source>Additional output name collisions omitted.</source>
        <translation>已省略其他输出名称冲突。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="302"/>
        <source>Loading SCRFD model...</source>
        <translation>正在加载 SCRFD 模型...</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="306"/>
        <source>Reusing loaded SCRFD model.</source>
        <translation>正在复用已加载的 SCRFD 模型。</translation>
    </message>
    <message>
        <source>Loading face detection model...</source>
        <translation>正在加载人脸检测模型...</translation>
    </message>
    <message>
        <source>Reusing loaded face detection model.</source>
        <translation>正在复用已加载的人脸检测模型。</translation>
    </message>
    <message>
        <source>Face detection backend: %1</source>
        <translation>人脸检测后端：%1</translation>
    </message>
    <message>
        <source>GPU acceleration can't run the face model; using the CPU instead.</source>
        <translation>GPU 加速无法运行人脸模型，已改用 CPU。</translation>
    </message>
    <message>
        <source>License plate detection backend: %1</source>
        <translation>车牌检测后端：%1</translation>
    </message>
    <message>
        <source>Loading license plate detection model...</source>
        <translation>正在加载车牌检测模型...</translation>
    </message>
    <message>
        <source>Reusing loaded license plate detection model.</source>
        <translation>正在复用已加载的车牌检测模型。</translation>
    </message>
    <message>
        <source>GPU acceleration can't run the license plate model; using the CPU instead.</source>
        <translation>GPU 加速无法运行车牌模型，已改用 CPU。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="309"/>
        <source>Scanning inputs...</source>
        <translation>正在扫描输入...</translation>
    </message>
    <message numerus="yes">
        <source>Preflight: found %n supported file(s).</source>
        <translation><numerusform>预检：找到 %n 个支持的文件。</numerusform></translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="323"/>
        <source>No supported files were found.</source>
        <translation>未找到支持的文件。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="333"/>
        <source>Cannot create output directory: %1</source>
        <translation>无法创建输出目录：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="348"/>
        <source>Refusing to run because multiple inputs would write to the same output path.</source>
        <translation>拒绝运行，因为多个输入将写入同一输出路径。</translation>
    </message>
    <message>
        <source>Refusing to run because an output path is already in use.</source>
        <translation>拒绝运行，因为某个输出路径已被占用。</translation>
    </message>
    <message>
        <source>Existing output would be overwritten: &apos;%1&apos;</source>
        <translation>现有输出将被覆盖：“%1”</translation>
    </message>
    <message>
        <source>Additional output conflicts omitted.</source>
        <translation>已省略其他输出冲突。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="356"/>
        <source>Preflight: output paths are unique.</source>
        <translation>预检：输出路径互不重复。</translation>
    </message>
    <message>
        <source>Preflight: output paths are available.</source>
        <translation>预检：输出路径可用。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="378"/>
        <source>Skipped unsafe output path for: %1</source>
        <translation>因输出路径不安全而跳过：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="390"/>
        <source>Skipped (cannot create parent dir): %1 — %2</source>
        <translation>已跳过（无法创建父目录）：%1 — %2</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="403"/>
        <source>Skipped (file too large, %1 MB): %2</source>
        <translation>已跳过（文件过大，%1 MB）：%2</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="412"/>
        <source>Loading</source>
        <translation>正在加载</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="417"/>
        <source>Skipped (%1): %2</source>
        <translation>已跳过（%1）：%2</translation>
    </message>
    <message>
        <source>Skipped (animated or multi-page images are not supported): %1</source>
        <translation>已跳过（不支持动画或多页图像）：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="427"/>
        <source>Skipped unreadable image: %1</source>
        <translation>已跳过无法读取的图像：%1</translation>
    </message>
    <message>
        <source>Source file changed during processing: %1</source>
        <translation>源文件在处理过程中发生了更改：%1</translation>
    </message>
    <message>
        <source>Failed to create a private source snapshot: %1</source>
        <translation>无法创建源文件的私有快照：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="443"/>
        <source>Skipped (image too large, %1 × %2): %3</source>
        <translation>已跳过（图像过大，%1 × %2）：%3</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="452"/>
        <source>Detecting</source>
        <translation>正在检测</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="464"/>
        <source>Reviewing</source>
        <translation>正在检查</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="483"/>
        <source>Review bridge unavailable; saved without review.</source>
        <translation>检查桥接不可用；已跳过检查并保存。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="514"/>
        <source>Skipped without saving: %1</source>
        <translation>未保存并跳过：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="522"/>
        <source>Saving</source>
        <translation>正在保存</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="525"/>
        <source>Failed to copy: %1</source>
        <translation>复制失败：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="530"/>
        <source>Skipped (original copied): %1</source>
        <translation>已跳过（已复制原文件）：%1</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="537"/>
        <source>Applying mosaic</source>
        <translation>正在应用马赛克</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="548"/>
        <source>Failed to save: %1</source>
        <translation>保存失败：%1</translation>
    </message>
    <message numerus="yes">
        <source>Redacted %n region(s): %1</source>
        <translation><numerusform>已遮盖 %n 个区域：%1</numerusform></translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="548"/>
        <source>Saved, but could not copy metadata: %1</source>
        <translation>已保存，但无法复制元数据：%1</translation>
    </message>
    <message>
        <source>Saved with no regions redacted: %1</source>
        <translation>保存时未遮盖任何区域：%1</translation>
    </message>
    <message>
        <source>Skipped (source and destination are the same file): %1</source>
        <translation>已跳过（源文件与目标文件相同）：%1</translation>
    </message>
    <message>
        <source>Error processing %1: %2</source>
        <translation>处理 %1 时出错：%2</translation>
    </message>
    <message>
        <source>Unexpected error — processing stopped.</source>
        <translation>意外错误——处理已停止。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="562"/>
        <source>Summary: %1 redacted, %2 saved without redaction, %3 copied, %4 skipped, %5 failed (of %6).</source>
        <translation>摘要：已遮盖 %1 个，未遮盖保存 %2 个，已复制 %3 个，已跳过 %4 个，失败 %5 个（共 %6 个）。</translation>
    </message>
    <message numerus="yes">
        <source>Warning: %n image(s) were saved with no regions redacted. Check them before sharing.</source>
        <translation><numerusform>警告：%n 张图像在未遮盖任何区域的情况下保存。请在分享前检查。</numerusform></translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="571"/>
        <source>Done.</source>
        <translation>完成。</translation>
    </message>
    <message>
        <source>Completed with warnings. Review the summary before sharing.</source>
        <translation>已完成，但有警告。请在分享前检查摘要。</translation>
    </message>
    <message>
        <location filename="../src/ProcessorWorker.cpp" line="575"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <source>Error processing %1</source>
        <translation>处理 %1 时出错</translation>
    </message>
    <message>
        <source>Reviewing video tracks</source>
        <translation>正在检查视频轨迹</translation>
    </message>
    <message>
        <source>Metadata preservation is not available for videos; metadata was removed: %1</source>
        <translation>视频无法保留元数据；已移除元数据：%1</translation>
    </message>
    <message>
        <source>Failed (%1): %2</source>
        <translation>失败（%1）：%2</translation>
    </message>
    <message>
        <source>Inspecting</source>
        <translation>正在检查</translation>
    </message>
    <message>
        <source>Failed (unsupported video: %1): %2</source>
        <translation>失败（不支持的视频：%1）：%2</translation>
    </message>
    <message>
        <source>Note: variable frame rate is converted to a constant frame rate: %1</source>
        <translation>注意：可变帧率将转换为恒定帧率：%1</translation>
    </message>
    <message>
        <source>Analyzing %1%</source>
        <translation>正在分析 %1%</translation>
    </message>
    <message>
        <source>Encoding %1%</source>
        <translation>正在编码 %1%</translation>
    </message>
    <message>
        <source>Failed to process video %1: %2</source>
        <translation>处理视频 %1 失败：%2</translation>
    </message>
    <message>
        <source>Loading face detection model for video...</source>
        <translation>正在加载视频人脸检测模型...</translation>
    </message>
    <message>
        <source>GPU acceleration can't run the video face model at %1 px; using the CPU instead.</source>
        <translation>GPU 加速无法以 %1 像素运行视频人脸模型，已改用 CPU。</translation>
    </message>
    <message>
        <source>Video face detection: %1 px · %2</source>
        <translation>视频人脸检测：%1 像素 · %2</translation>
    </message>
    <message>
        <source>%1m %2s left</source>
        <translation>剩余 %1 分 %2 秒</translation>
    </message>
    <message>
        <source>%1s left</source>
        <translation>剩余 %1 秒</translation>
    </message>
    <message>
        <source>Processed %1 frames in %2s (%3× real time): %4</source>
        <translation>已处理 %1 帧，用时 %2 秒（实时速度的 %3 倍）：%4</translation>
    </message>
    <message>
        <source>Video encoder: %1</source>
        <translation>视频编码器：%1</translation>
    </message>
</context>
<context>
    <name>cloakframe::ReviewDialog</name>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="370"/>
        <source>Review — %1</source>
        <translation>检查 — %1</translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="389"/>
        <source>Click or Return toggles a box · Drag an empty area to add · Arrow keys move the selection · Hold Space to preview the result · Scroll to zoom, right-drag to pan, 0 resets · %1 / %2 to undo/redo · Esc skips this image without saving</source>
        <translation>单击或按 Return 切换检测框 · 在空白区域拖动以添加 · 方向键移动所选框 · 按住空格预览结果 · 滚轮缩放，右键拖动平移，按 0 重置 · %1 / %2 撤销/重做 · Esc 跳过此图像且不保存</translation>
    </message>
    <message>
        <source>Review image</source>
        <translation>检查图像</translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="399"/>
        <source>Cancel All</source>
        <translation>全部取消</translation>
    </message>
    <message>
        <source>Cancel All?</source>
        <translation>全部取消？</translation>
    </message>
    <message numerus="yes">
        <source>Stop reviewing and cancel the remaining %n image(s)?

Images already saved are kept.</source>
        <translation><numerusform>停止检查并取消剩余的 %n 张图像？

已保存的图像会保留。</numerusform></translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="402"/>
        <source>Undo</source>
        <translation>撤销</translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="406"/>
        <source>Redo</source>
        <translation>重做</translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="410"/>
        <source>Do Not Save</source>
        <translation>不保存</translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="413"/>
        <source>Copy Original</source>
        <translation>复制原图</translation>
    </message>
    <message>
        <source>Saves the image without anonymizing it.</source>
        <translation>保存图像但不进行匿名化。</translation>
    </message>
    <message>
        <source>Copy Original?</source>
        <translation>复制原图？</translation>
    </message>
    <message>
        <source>This image will not be anonymized.

%1

Continue?</source>
        <translation>此图像不会被匿名化。

%1

继续？</translation>
    </message>
    <message>
        <source>The unredacted original will be saved to the output folder, including its original metadata (EXIF, GPS, timestamps).</source>
        <translation>未遮盖的原图及其原始元数据（EXIF、GPS、时间戳）将保存到输出文件夹。</translation>
    </message>
    <message>
        <source>The unredacted original will be saved to the output folder (re-encoded without metadata).</source>
        <translation>未遮盖的原图将保存到输出文件夹（重新编码且不含元数据）。</translation>
    </message>
    <message>
        <location filename="../src/ReviewDialog.cpp" line="416"/>
        <source>Save &amp;&amp; Next</source>
        <translation>保存并继续</translation>
    </message>
</context>
<context>
    <name>cloakframe::SettingsDialog</name>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="78"/>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="79"/>
        <source>Theme</source>
        <translation>主题</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="80"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="81"/>
        <source>System</source>
        <translation>跟随系统</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="82"/>
        <source>Light</source>
        <translation>浅色</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="83"/>
        <source>Dark</source>
        <translation>深色</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="84"/>
        <source>Check for updates on startup</source>
        <translation>启动时检查更新</translation>
    </message>
    <message>
        <source>Write a local log file</source>
        <translation>写入本地日志文件</translation>
    </message>
    <message>
        <source>The log may include the names of files you process. Stored on this device only. Takes effect on the next launch.</source>
        <translation>日志可能包含您处理的文件名。日志仅存储在此设备上，并在下次启动时生效。</translation>
    </message>
    <message>
        <source>Use GPU acceleration</source>
        <translation>使用 GPU 加速</translation>
    </message>
    <message>
        <source>Runs detection models and video encoding on the GPU when available. Applies from the next run.</source>
        <translation>可用时使用 GPU 运行检测模型和视频编码。从下次运行起生效。</translation>
    </message>
    <message>
        <source>Video quality</source>
        <translation>视频质量</translation>
    </message>
    <message>
        <source>High (near-original)</source>
        <translation>高（接近原始质量）</translation>
    </message>
    <message>
        <source>Balanced</source>
        <translation>均衡</translation>
    </message>
    <message>
        <source>Smaller files</source>
        <translation>较小文件</translation>
    </message>
    <message>
        <source>Quality of re-encoded videos. Higher quality produces larger files.</source>
        <translation>重新编码视频的质量。质量越高，文件越大。</translation>
    </message>
    <message>
        <source>Video codec</source>
        <translation>视频编解码器</translation>
    </message>
    <message>
        <source>H.264 (most compatible)</source>
        <translation>H.264（兼容性最佳）</translation>
    </message>
    <message>
        <source>HEVC (smaller files)</source>
        <translation>HEVC（文件更小）</translation>
    </message>
    <message>
        <source>Codec for re-encoded videos. HEVC produces smaller files but may not play on older devices.</source>
        <translation>重新编码视频使用的编解码器。HEVC 文件更小，但可能无法在旧设备上播放。</translation>
    </message>
    <message>
        <location filename="../src/SettingsDialog.cpp" line="87"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
</context>
<context>
    <name>cloakframe::VideoIo</name>
    <message>
        <source>FFmpeg was not found. Video processing is unavailable.</source>
        <translation>未找到 FFmpeg，无法处理视频。</translation>
    </message>
    <message>
        <source>FFmpeg was found but could not be executed.</source>
        <translation>已找到 FFmpeg，但无法执行。</translation>
    </message>
    <message>
        <source>Could not read the FFmpeg checksum manifest.</source>
        <translation>无法读取 FFmpeg 校验和清单。</translation>
    </message>
    <message>
        <source>Could not read the bundled FFmpeg binary.</source>
        <translation>无法读取随附的 FFmpeg 二进制文件。</translation>
    </message>
    <message>
        <source>The bundled FFmpeg binary failed its integrity check.</source>
        <translation>随附的 FFmpeg 二进制文件未通过完整性检查。</translation>
    </message>
    <message>
        <source>Could not inspect the video (ffprobe did not respond).</source>
        <translation>无法检查视频（ffprobe 未响应）。</translation>
    </message>
    <message>
        <source>Could not inspect the video: %1</source>
        <translation>无法检查视频：%1</translation>
    </message>
    <message>
        <source>The file contains no video stream.</source>
        <translation>文件不包含视频流。</translation>
    </message>
    <message>
        <source>the video stream could not be read</source>
        <translation>无法读取视频流</translation>
    </message>
    <message>
        <source>unsupported video codec '%1' (H.264/HEVC only)</source>
        <translation>不支持的视频编解码器“%1”（仅支持 H.264/HEVC）</translation>
    </message>
    <message>
        <source>10-bit or higher bit depth is not supported yet</source>
        <translation>暂不支持 10 位或更高位深度</translation>
    </message>
    <message>
        <source>HDR video is not supported yet</source>
        <translation>暂不支持 HDR 视频</translation>
    </message>
    <message>
        <source>Invalid video dimensions.</source>
        <translation>视频尺寸无效。</translation>
    </message>
    <message>
        <source>Could not start FFmpeg for decoding.</source>
        <translation>无法启动 FFmpeg 进行解码。</translation>
    </message>
    <message>
        <source>Decoding failed: %1</source>
        <translation>解码失败：%1</translation>
    </message>
    <message>
        <source>Decoding ended mid-frame: %1</source>
        <translation>解码在帧中途结束：%1</translation>
    </message>
    <message>
        <source>Decoding timed out.</source>
        <translation>解码超时。</translation>
    </message>
    <message>
        <source>Could not start FFmpeg for encoding.</source>
        <translation>无法启动 FFmpeg 进行编码。</translation>
    </message>
    <message>
        <source>Could not create a temporary directory for encoding.</source>
        <translation>无法创建用于编码的临时目录。</translation>
    </message>
    <message>
        <source>Encoding failed: %1</source>
        <translation>编码失败：%1</translation>
    </message>
    <message>
        <source>Internal error: frame does not match the video format.</source>
        <translation>内部错误：帧与视频格式不匹配。</translation>
    </message>
    <message>
        <source>Encoding timed out while finalizing.</source>
        <translation>编码在完成阶段超时。</translation>
    </message>
    <message>
        <source>Could not move the finished video into place.</source>
        <translation>无法将完成的视频移动到目标位置。</translation>
    </message>
    <message>
        <source>The output file already exists.</source>
        <translation>输出文件已存在。</translation>
    </message>
    <message>
        <source>the video resolution exceeds the safety limit</source>
        <translation>视频分辨率超过安全限制</translation>
    </message>
    <message>
        <source>the video frame rate exceeds the safety limit</source>
        <translation>视频帧率超过安全限制</translation>
    </message>
    <message>
        <source>the video duration exceeds the safety limit</source>
        <translation>视频时长超过安全限制</translation>
    </message>
    <message>
        <source>the video frame count exceeds the safety limit</source>
        <translation>视频帧数超过安全限制</translation>
    </message>
    <message>
        <source>The source video changed during processing.</source>
        <translation>源视频在处理过程中发生了更改。</translation>
    </message>
</context>
<context>
    <name>cloakframe::VideoReviewCanvas</name>
    <message>
        <source>Could not load this frame preview.</source>
        <translation>无法加载此帧预览。</translation>
    </message>
    <message>
        <source>Track %1</source>
        <translation>轨迹 %1</translation>
    </message>
</context>
<context>
    <name>cloakframe::VideoReviewDialog</name>
    <message>
        <source>Review video tracks — %1</source>
        <translation>检查视频轨迹 — %1</translation>
    </message>
    <message>
        <source>Scrub the timeline and uncheck false detections. Changes apply to the entire track before the video is encoded.</source>
        <translation>浏览时间线并取消勾选错误检测。更改将在视频编码前应用到整个轨迹。</translation>
    </message>
    <message>
        <source>Track %1  ·  %2–%3</source>
        <translation>轨迹 %1  ·  %2–%3</translation>
    </message>
    <message>
        <source>Include all</source>
        <translation>全部包括</translation>
    </message>
    <message>
        <source>Exclude all</source>
        <translation>全部排除</translation>
    </message>
    <message>
        <source>Cancel all</source>
        <translation>全部取消</translation>
    </message>
    <message>
        <source>Encode video</source>
        <translation>编码视频</translation>
    </message>
    <message>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <source>%1 of %2 tracks included</source>
        <translation>已包括 %1 / %2 条轨迹</translation>
    </message>
</context>
<context>
    <name>cloakframe::VideoProcessor</name>
    <message>
        <source>No frames could be decoded.</source>
        <translation>无法解码任何帧。</translation>
    </message>
    <message>
        <source>Could not inspect the source video.</source>
        <translation>无法检查源视频。</translation>
    </message>
    <message>
        <source>Could not create a private snapshot of the source video.</source>
        <translation>无法创建源视频的私有快照。</translation>
    </message>
    <message>
        <source>The source video changed during processing. Start the operation again.</source>
        <translation>源视频在处理过程中发生了更改。请重新开始操作。</translation>
    </message>
    <message>
        <source>The video frame count exceeds the safety limit.</source>
        <translation>视频帧数超过安全限制。</translation>
    </message>
    <message>
        <source>Video detection data exceeds the safety limit.</source>
        <translation>视频检测数据超过安全限制。</translation>
    </message>
    <message>
        <source>Video tracking data exceeds the safety limit.</source>
        <translation>视频跟踪数据超过安全限制。</translation>
    </message>
    <message>
        <source>The source video changed during processing (frame count differs between passes).</source>
        <translation>源视频在处理过程中发生了更改（两次处理的帧数不同）。</translation>
    </message>
</context>
</TS>
