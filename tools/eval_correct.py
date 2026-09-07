import csv
import glob
import json
import os
import subprocess
import sys
import tempfile
import urllib.request

import cv2
import numpy as np

SERVER = "http://127.0.0.1:8080"
TMP = os.path.join(tempfile.gettempdir(), "board_eval")
os.makedirs(TMP, exist_ok=True)


def measure_residual(img):
    if img is None:
        return 99.0
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    g = cv2.GaussianBlur(g, (5, 5), 0)
    _, b = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    k = cv2.getStructuringElement(cv2.MORPH_RECT, (25, 1))
    b = cv2.dilate(b, k)
    cs, _ = cv2.findContours(b, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    angles = []
    for c in cs:
        if len(c) < 8:
            continue
        r = cv2.minAreaRect(c)
        if max(r[1]) < 40:
            continue
        a = r[2]
        if r[1][0] < r[1][1]:
            a += 90
        elif a < -45:
            a += 90
        while a > 45:
            a -= 90
        while a < -45:
            a += 90
        angles.append(a)
    if not angles:
        return 0.0
    angles.sort()
    return abs(angles[len(angles) // 2])


def rotate_full(img, deg):
    h, w = img.shape[:2]
    rad = np.deg2rad(deg)
    nw = int(np.ceil(w * abs(np.cos(rad)) + h * abs(np.sin(rad))))
    nh = int(np.ceil(w * abs(np.sin(rad)) + h * abs(np.cos(rad))))
    m = cv2.getRotationMatrix2D((w / 2, h / 2), deg, 1.0)
    m[0, 2] += (nw - w) / 2
    m[1, 2] += (nh - h) / 2
    return cv2.warpAffine(img, m, (nw, nh), borderValue=(255, 255, 255))


def perspective_warp(img):
    h, w = img.shape[:2]
    src = np.float32([[0, 0], [w, 0], [w, h], [0, h]])
    dx = int(w * np.random.uniform(0.04, 0.14))
    dy = int(h * np.random.uniform(0.02, 0.10))
    dst = np.float32([[dx, dy], [w - dx * np.random.uniform(0.3, 1.0), dy * 0.3],
                      [w - dx * np.random.uniform(0.2, 0.8), h - dy],
                      [dx * np.random.uniform(0.3, 1.0), h - dy * 0.7]])
    m = cv2.getPerspectiveTransform(src, dst)
    return cv2.warpPerspective(img, m, (w, h), borderValue=(235, 235, 235))


def make_variants(img, tag):
    variants = {}
    for deg in (6, 10, 18):
        variants[f"{tag}_rot{deg}"] = rotate_full(img, deg)
    for i in range(2):
        variants[f"{tag}_persp{i}"] = perspective_warp(img)
    variants[f"{tag}_dark50"] = np.clip(img.astype(np.float32) * 0.5, 0, 255).astype(np.uint8)
    variants[f"{tag}_dark70"] = np.clip(img.astype(np.float32) * 0.7, 0, 255).astype(np.uint8)
    variants[f"{tag}_blur3"] = cv2.GaussianBlur(img, (0, 0), 3)
    variants[f"{tag}_blur6"] = cv2.GaussianBlur(img, (0, 0), 6)
    return variants


def call_correct(path):
    out = subprocess.run(
        ["curl.exe", "-s", "--max-time", "300", "-X", "POST",
         "-F", f"images=@{path}", f"{SERVER}/api/correct"],
        capture_output=True, text=True)
    try:
        return json.loads(out.stdout)
    except Exception:
        return None


def fetch_image(url):
    try:
        with urllib.request.urlopen(SERVER + url, timeout=60) as r:
            buf = np.frombuffer(r.read(), dtype=np.uint8)
            return cv2.imdecode(buf, cv2.IMREAD_COLOR)
    except Exception:
        return None


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "D:/aic/test_images/board"
    images = []
    for ext in ("*.jpg", "*.jpeg", "*.png"):
        images += glob.glob(os.path.join(folder, ext))
    if not images:
        print("no images in", folder)
        return
    print("source images:", len(images))

    rows = []
    total = 0
    ok = 0
    for img_path in images:
        img = cv2.imread(img_path)
        if img is None:
            continue
        base = os.path.splitext(os.path.basename(img_path))[0]
        variants = make_variants(img, base)
        for name, var in variants.items():
            p = os.path.join(TMP, name + ".jpg")
            cv2.imwrite(p, var, [cv2.IMWRITE_JPEG_QUALITY, 90])
            resp = call_correct(p)
            if not resp or not resp.get("results"):
                rows.append([name, "server-fail", "", "", ""])
                continue
            item = resp["results"][0]
            mode = item["mode"]
            out_img = fetch_image(item["url"])
            resid = measure_residual(out_img)
            success = (mode == "perspective") or (resid <= 1.5)
            total += 1
            if success:
                ok += 1
            rows.append([name, mode, round(resid, 2), success, item["width"],
                         item["height"]])

    if total:
        print(f"\n总体成功率: {ok}/{total} = {ok*100.0/total:.1f}%")
    else:
        print("no test runs recorded")

    with open("eval_report.csv", "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["variant", "mode", "residual_deg", "success", "out_w", "out_h"])
        w.writerows(rows)
    print("report saved: eval_report.csv")


if __name__ == "__main__":
    main()
