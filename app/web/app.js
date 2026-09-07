"use strict";

const dropzone = document.getElementById("dropzone");
const fileInput = document.getElementById("fileInput");
const statusBox = document.getElementById("status");
const gallery = document.getElementById("gallery");

const MODE_LABEL = {
  perspective: "透视校正",
  deskew: "倾斜摆正",
  none: "无需矫正"
};

function showStatus(text, type, spin) {
  statusBox.innerHTML = "";
  if (spin) {
    const sp = document.createElement("span");
    sp.className = "spinner";
    statusBox.appendChild(sp);
  }
  statusBox.appendChild(document.createTextNode(text));
  statusBox.className = "status " + (type || "info");
  statusBox.classList.remove("hidden");
}

function hideStatus() {
  statusBox.classList.add("hidden");
}

function escapeHtml(s) {
  return s.replace(/[&<>"']/g, (c) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;"
  }[c]));
}

function makeOriginalEditor(file, objectUrl, corners) {
  const wrap = document.createElement("div");
  wrap.className = "orig-edit";

  const img = document.createElement("img");
  img.alt = "原图";
  img.src = objectUrl;
  wrap.appendChild(img);

  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("class", "quad-overlay");
  const poly = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
  svg.appendChild(poly);
  wrap.appendChild(svg);

  let natural = { w: 1, h: 1 };
  let pts = corners ? corners.slice()
    : [{ x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }];

  img.addEventListener("load", () => {
    natural = { w: img.naturalWidth, h: img.naturalHeight };
    if (!corners) {
      pts = [
        { x: 0, y: 0 }, { x: natural.w, y: 0 },
        { x: natural.w, y: natural.h }, { x: 0, y: natural.h }
      ];
    }
    update();
  });

  const handles = [];
  for (let i = 0; i < 4; i++) {
    const hd = document.createElement("div");
    hd.className = "corner-handle";
    wrap.appendChild(hd);

    hd.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      hd.setPointerCapture(e.pointerId);
      const move = (ev) => {
        const rect = img.getBoundingClientRect();
        const x = (ev.clientX - rect.left) / rect.width * natural.w;
        const y = (ev.clientY - rect.top) / rect.height * natural.h;
        pts[i] = {
          x: Math.max(0, Math.min(natural.w, x)),
          y: Math.max(0, Math.min(natural.h, y))
        };
        update();
      };
      const up = () => {
        hd.removeEventListener("pointermove", move);
        hd.removeEventListener("pointerup", up);
      };
      hd.addEventListener("pointermove", move);
      hd.addEventListener("pointerup", up);
    });
    handles.push(hd);
  }

  function update() {
    const rect = img.getBoundingClientRect();
    const sx = rect.width / natural.w;
    const sy = rect.height / natural.h;
    handles.forEach((hd, i) => {
      hd.style.left = (pts[i].x * sx - 8) + "px";
      hd.style.top = (pts[i].y * sy - 8) + "px";
    });
    poly.setAttribute("points",
      pts.map((p) => (p.x * sx) + "," + (p.y * sy)).join(" "));
  }

  return { wrap, getCorners: () => pts, onResize: update };
}

function addCard(item, objectUrl, file) {
  const card = document.createElement("div");
  card.className = "card";
  const tagCls = item.mode === "perspective" ? " perspective"
    : item.mode === "deskew" ? " deskew" : "";
  const modeText = MODE_LABEL[item.mode] || item.mode;
  const angleText = item.mode === "deskew"
    ? "，" + Math.abs(item.angle).toFixed(1) + "°" : "";

  const origWrap = document.createElement("div");
  origWrap.className = "preview-side";
  origWrap.innerHTML = "<figcaption>原图（可拖动角点）</figcaption>";
  const editor = makeOriginalEditor(file, objectUrl, item.corners);
  origWrap.appendChild(editor.wrap);

  card.innerHTML =
    '<div class="card-head"><span>' + escapeHtml(item.name || "") +
    '</span><span class="head-right">' +
    '<span class="tag' + tagCls + '">' + modeText + angleText + '</span>' +
    '<button class="del-btn" title="删除这张照片">×</button></span></div>' +
    '<div class="preview">' +
    '<div class="preview-side result-side">' +
    '<figcaption>矫正增强</figcaption>' +
    '<img alt="结果" src="' + item.url + '">' +
    "</div>" +
    '</div>' +
    '<div class="card-actions">' +
    '<button class="btn manual-btn">手动矫正</button>' +
    '<button class="btn ghost">查看大图</button>' +
    '<a class="btn" href="' + item.url + '" download="board_' +
    (item.name ? item.name : "image.jpg") + '">下载</a>' +
    '<button class="btn note-btn">识别·生成笔记</button>' +
    "</div>" +
    '<div class="note-panel hidden">' +
    '<div class="note-img"></div>' +
    '<div class="note-text"></div>' +
    "</div>";

  const preview = card.querySelector(".preview");
  preview.insertBefore(origWrap, preview.firstChild);

  const resultImg = card.querySelector(".result-side img");
  const downloadLink = card.querySelector(".card-actions a.btn");
  const openBtn = card.querySelector(".btn.ghost");

  const refreshOpen = () => {
    openBtn.onclick = () => {
      fetch(resultImg.src)
        .then((r) => r.blob())
        .then((blob) => {
          const raw = URL.createObjectURL(blob);
          window.open(raw, "_blank");
          setTimeout(() => URL.revokeObjectURL(raw), 60000);
        });
    };
  };
  refreshOpen();

  const manualBtn = card.querySelector(".manual-btn");
  manualBtn.addEventListener("click", () => {
    manualBtn.disabled = true;
    manualBtn.textContent = "矫正中…";
    const form = new FormData();
    form.append("image", file, file.name);
    form.append("corners", JSON.stringify(editor.getCorners()));
    fetch("/api/correct_manual", { method: "POST", body: form })
      .then((res) => res.json().catch(() => null))
      .then((data) => {
        if (!data || !data.url) {
          showStatus("手动矫正失败，请检查角点位置", "error");
          return;
        }
        resultImg.src = data.url;
        downloadLink.href = data.url;
        card.querySelector(".tag").textContent = "手动矫正";
        card.querySelector(".tag").className = "tag manual";
        refreshOpen();
        showStatus("手动矫正完成", "info");
      })
      .catch(() => showStatus("手动矫正请求失败", "error"))
      .finally(() => {
        manualBtn.disabled = false;
        manualBtn.textContent = "手动矫正";
      });
  });

  window.addEventListener("resize", () => editor.onResize());

  const noteBtn = card.querySelector(".note-btn");
  const notePanel = card.querySelector(".note-panel");
  const noteImg = card.querySelector(".note-img");
  const noteText = card.querySelector(".note-text");

  let lastMarkdown = "";
  let lastTitle = "";

  function printPdf() {
    const bodyHtml = (typeof window.marked === "function")
      ? window.marked.parse(lastMarkdown)
      : "<pre>" + escapeHtml(lastMarkdown) + "</pre>";
    const win = window.open("", "_blank");
    if (!win) {
      showStatus("浏览器拦截了弹窗，请允许后重试", "error");
      return;
    }
    win.document.write(
      "<!DOCTYPE html><html><head><meta charset='utf-8'>" +
      "<title>" + escapeHtml(lastTitle) + "</title>" +
      "<style>body{font-family:'Microsoft YaHei',sans-serif;max-width:800px;" +
      "margin:32px auto;line-height:1.8;color:#1e293b}" +
      "h1{font-size:22px;border-bottom:2px solid #2563eb;padding-bottom:8px}" +
      "h2{font-size:17px;margin-top:18px}" +
      "table{border-collapse:collapse;width:100%;margin:10px 0}" +
      "th,td{border:1px solid #ccc;padding:6px 10px;text-align:left}" +
      "blockquote{border-left:3px solid #94a3b8;padding-left:12px;color:#64748b}" +
      "code{background:#eef2f7;padding:1px 5px;border-radius:4px}" +
      "img{max-width:100%}" +
      "@media print{body{margin:12mm}}</style></head><body>" +
      "<h1>" + escapeHtml(lastTitle) + "</h1>" + bodyHtml +
      "<p style='color:#94a3b8;font-size:11px;margin-top:24px'>由智影课堂 AI 自动生成</p>" +
      "</body></html>"
    );
    win.document.close();
    setTimeout(() => {
      try { win.focus(); win.print(); } catch (e) { /* ignore */ }
    }, 500);
  }

  function genNote() {
    if (!file) {
      showStatus("找不到原图文件，请重新上传", "error");
      return Promise.resolve(false);
    }
    noteBtn.disabled = true;
    noteBtn.textContent = "处理中…";
    notePanel.classList.remove("hidden");
    noteText.innerHTML = "<p>正在识别板书并整理笔记，请稍候…</p>";
    const startTime = Date.now();
    const ticker = setInterval(() => {
      const secs = Math.round((Date.now() - startTime) / 1000);
      noteText.innerHTML = "<p>正在识别板书并整理笔记，请稍候…（已等待 " + secs +
        " 秒，复杂板书需要更久，请勿重复点击）</p>";
    }, 5000);

    const ctrl = new AbortController();
    const watchdog = setTimeout(() => ctrl.abort(), 150000);

    const form = new FormData();
    form.append("image", file, file.name);
    return fetch("/api/note", { method: "POST", body: form, signal: ctrl.signal })
      .then((res) => res.json().catch(() => null))
      .then((data) => {
        if (!data) {
          noteText.innerHTML = "<p class='err'>服务器返回异常</p>";
          return false;
        }
        if (data.imageUrl) {
          noteImg.innerHTML = "<img src='" + data.imageUrl + "' alt='处理结果'>";
        }
        let html = "";
        const chips = [];
        if (data.mode) chips.push(MODE_LABEL[data.mode] || data.mode);
        if (data.blur !== undefined) {
          if (data.blurAfter !== undefined) {
            chips.push("清晰度 " + data.blur.toFixed(0) + " → " +
              data.blurAfter.toFixed(0));
          } else {
            chips.push("清晰度 " + data.blur.toFixed(0));
          }
          if (data.blur < 60) {
            html += "<p class='warn'>检测到照片较模糊，识别准确率会下降，建议重拍或靠近拍摄。</p>";
          }
        }
        if (data.ocr && data.ocr.text) chips.push("识别 " + data.ocr.text.length + " 字");
        if (chips.length) {
          html += "<div class='chips'>" + chips.map((c) =>
            "<span class='chip'>" + escapeHtml(c) + "</span>").join("") + "</div>";
        }
        if (data.steps && data.steps.length) {
          html += "<h4>处理过程</h4><div class='steps'>";
          data.steps.forEach((s, i) => {
            html += "<figure><span class='badge'>" + (i + 1) + "</span>" +
              "<img src='" + s.url + "' alt=''>" +
              "<figcaption>" + escapeHtml(s.label) + "</figcaption></figure>";
          });
          html += "</div>";
        }
        if (data.ocr && data.ocr.status === "ok") {
          html += "<h4>识别原文</h4><pre>" + escapeHtml(data.ocr.text) + "</pre>";
        } else if (data.ocr && data.ocr.status === "no_key") {
          html += "<p class='err'>在线 OCR 未配置密钥（config.json）</p>";
        } else if (data.ocr) {
          html += "<p class='err'>OCR 失败：" +
            escapeHtml(data.ocr.detail || "未知错误") + "</p>";
        }
        if (data.note && data.note.status === "ok") {
          lastMarkdown = data.note.markdown;
          lastTitle = item.name || "课堂笔记";
          const blob = new Blob([lastMarkdown], { type: "text/markdown" });
          const mdUrl = URL.createObjectURL(blob);
          const mdBody = (typeof window.marked === "function")
            ? '<div class="md-body">' + window.marked.parse(lastMarkdown) + "</div>"
            : '<pre class="md">' + escapeHtml(lastMarkdown) + "</pre>";
          html += "<h4>结构化笔记</h4>" + mdBody +
            "<div style='display:flex;gap:10px;flex-wrap:wrap'>" +
            "<a class='btn small' href='" + mdUrl + "' download='note.md'>下载 .md</a>" +
            "<button class='btn small' id='pdf-" + item.id + "'>导出 PDF</button>" +
            "</div>";
          setTimeout(() => {
            const b = document.getElementById("pdf-" + item.id);
            if (b) b.addEventListener("click", printPdf);
          }, 0);
        } else if (data.note && data.note.status === "no_key") {
          html += "<p class='err'>LLM 未配置密钥，仅完成识别</p>";
        } else if (data.note) {
          html += "<p class='err'>笔记整理失败：" +
            escapeHtml(data.note.detail || "未知错误") + "</p>";
        }
        noteText.innerHTML = html || "<p>无内容</p>";
        return !!(data.note && data.note.status === "ok");
      })
      .catch((err) => {
        if (err && err.name === "AbortError") {
          noteText.innerHTML = "<p class='err'>处理超时（超过 2.5 分钟）。建议：勾选「快速模式」重试，或检查网络后重新生成。</p>";
        } else {
          noteText.innerHTML = "<p class='err'>请求失败：" + err.message + "</p>";
        }
        return false;
      })
      .finally(() => {
        clearInterval(ticker);
        clearTimeout(watchdog);
        noteBtn.disabled = false;
        noteBtn.textContent = "识别·生成笔记";
      });
  }

  noteBtn.addEventListener("click", () => { genNote(); });
  const cardCtx = { name: item.name, genNote };
  cardCtx.remove = () => {
    const idx = gCards.indexOf(cardCtx);
    if (idx >= 0) gCards.splice(idx, 1);
    card.remove();
    if (gCards.length === 0) toolbar.classList.add("hidden");
    showStatus("已删除：" + item.name, "info");
  };
  const delBtn = card.querySelector(".del-btn");
  delBtn.addEventListener("click", () => { cardCtx.remove(); });
  gCards.push(cardCtx);

  gallery.prepend(card);
  toolbar.classList.remove("hidden");
}

const toolbar = document.getElementById("toolbar");
const batchNoteBtn = document.getElementById("batchNoteBtn");
const batchInfo = document.getElementById("batchInfo");
const gCards = [];

batchNoteBtn.addEventListener("click", async () => {
  batchNoteBtn.disabled = true;
  let ok = 0;
  for (let i = 0; i < gCards.length; i++) {
    batchInfo.textContent = "正在生成 " + (i + 1) + "/" + gCards.length +
      "：" + gCards[i].name;
    const done = await gCards[i].genNote();
    if (done) ok++;
  }
  batchInfo.textContent = "批量完成：" + ok + "/" + gCards.length + " 份笔记";
  showStatus("批量生成完成：" + ok + " 成功，" + (gCards.length - ok) + " 失败",
    ok === gCards.length ? "info" : "error");
  batchNoteBtn.disabled = false;
});

function upload(files) {
  if (!files.length) return;
  hideStatus();
  const form = new FormData();
  for (const f of files) form.append("images", f, f.name);
  showStatus("正在矫正 " + files.length + " 张黑板照片，请稍候…", "info", true);

  fetch("/api/correct", { method: "POST", body: form })
    .then((res) => res.json().catch(() => null))
    .then((data) => {
      if (!data || !data.results) {
        showStatus("服务器返回异常，请重试", "error");
        return;
      }
      for (let i = 0; i < data.results.length; i++) {
        addCard(data.results[i], URL.createObjectURL(files[i]), files[i]);
      }
      const msg = "完成：" + data.accepted + " 张处理成功";
      showStatus(data.rejected ? msg + "，" + data.rejected + " 张失败" : msg);
    })
    .catch((err) => {
      showStatus("上传失败：" + err.message, "error");
    });
}

dropzone.addEventListener("click", () => fileInput.click());
dropzone.addEventListener("dragover", (e) => {
  e.preventDefault();
  dropzone.classList.add("dragover");
});
dropzone.addEventListener("dragleave", () => dropzone.classList.remove("dragover"));
dropzone.addEventListener("drop", (e) => {
  e.preventDefault();
  dropzone.classList.remove("dragover");
  upload(Array.from(e.dataTransfer.files).filter((f) => f.type.startsWith("image/")));
});
fileInput.addEventListener("change", () => {
  upload(Array.from(fileInput.files));
  fileInput.value = "";
});
