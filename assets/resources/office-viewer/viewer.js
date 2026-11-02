/* PDF.js 页查看器。只负责把宿主传入的 base64 PDF 渲染成分页 canvas。 */
(function () {
  "use strict";
  if (window.pdfjsLib) {
    window.pdfjsLib.GlobalWorkerOptions.workerSrc = "pdf.worker.min.js";
  }

  function base64ToBytes(base64) {
    var binary = atob(base64);
    var bytes = new Uint8Array(binary.length);
    for (var i = 0; i < binary.length; ++i)
      bytes[i] = binary.charCodeAt(i);
    return bytes;
  }

  window.showMessage = function (text) {
    var status = document.getElementById("status");
    status.textContent = text;
    status.style.display = "block";
    document.getElementById("pages").textContent = "";
  };

  window.renderPdf = async function (base64) {
    if (!window.pdfjsLib) {
      window.showMessage("PDF.js 没有加载");
      return;
    }
    if (!base64 || base64.length === 0) {
      window.showMessage("没有可显示的 PDF");
      return;
    }
    try {
      window.showMessage("正在渲染文档…");
      var data = base64ToBytes(base64);
      var pdf = await window.pdfjsLib.getDocument({ data: data }).promise;
      var host = document.getElementById("pages");
      host.textContent = "";
      var status = document.getElementById("status");
      status.style.display = "none";

      // 首次只渲染前 3 页，其余滚动到附近再渲染，避免大文档一次性占满 UI 线程。
      var rendered = {};
      var renderPage = async function (pageNo) {
        if (rendered[pageNo])
          return;
        rendered[pageNo] = true;
        var page = await pdf.getPage(pageNo);
        var viewport = page.getViewport({ scale: 1.6 });
        var wrapper = document.createElement("div");
        wrapper.className = "page";
        var canvas = document.createElement("canvas");
        canvas.width = viewport.width;
        canvas.height = viewport.height;
        wrapper.appendChild(canvas);
        var label = document.createElement("div");
        label.className = "pageno";
        label.textContent = pageNo + " / " + pdf.numPages;
        wrapper.appendChild(label);
        host.appendChild(wrapper);
        await page.render({ canvasContext: canvas.getContext("2d"), viewport: viewport }).promise;
      };

      for (var i = 1; i <= Math.min(3, pdf.numPages); ++i)
        await renderPage(i);

      if (pdf.numPages > 3) {
        var loading = false;
        var loadMore = async function () {
          if (loading)
            return;
          loading = true;
          var next = Object.keys(rendered).length + 1;
          for (var n = next; n <= Math.min(next + 2, pdf.numPages); ++n)
            await renderPage(n);
          loading = false;
        };
        window.addEventListener("scroll", function () {
          if (window.innerHeight + window.scrollY > document.body.scrollHeight - 900)
            loadMore();
        });
      }
    } catch (err) {
      window.showMessage("PDF 渲染失败：" + (err && err.message ? err.message : String(err)));
    }
  };
})();
