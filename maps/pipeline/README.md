# Pipeline Xây Dựng Bản Đồ Tự Động (DEM -> Map AAA 60 FPS)

Thư mục `maps/pipeline/` chứa toàn bộ công cụ trích xuất, phân tích thủy văn, sinh thái và sinh dữ liệu asset bản đồ tự động cho dự án Wuxing Skills.

## 1. Mục Tiêu Thiết Kế
- **Đầu vào tối giản**: Chỉ cần 1 tệp DEM GeoTIFF (`.tif`).
- **Không phụ thuộc thư viện nặng ngoài chuẩn**: Chạy bằng Python thuần + NumPy + Zlib tích hợp (không cần GDAL/Rasterio/PIL).
- **Đầu ra tiêu chuẩn hóa (Zero manual step)**:
  1. `[prefix]_island_heightmap.png`: Lưới độ cao 8-bit/16-bit tích hợp vách núi abyss.
  2. `[prefix]_ecology_splat.png`: Splatmap 4 kênh RGBA phân bố sinh thái (R: Cỏ, G: Vách đá/sỏi, B: Hoa dại, A: Sông/hồ/đầm lầy).
  3. `[prefix]_stream_network.json` & `[prefix]_stream_mask.png`: Tọa độ mạng lưới sông suối vector (đường tim và độ rộng).
  4. `[prefix]_generated_meta.h`: Header hằng số C chứa kích thước map, tọa độ hồ, cửa xả sông, đỉnh núi cao nhất.

## 2. Quy Trình 4 Bước
1. **Hydrological Analysis (Thủy văn & Lưu vực)**:
   - Priority-Flood / Wang-Liu sink depression filling: Xóa các hố trũng vi mô để dòng chảy thoát tự nhiên.
   - D8 Flow Direction & Flow Accumulation: Xác định lưu lượng tích tụ dòng chảy.
   - Stream Network Extraction: Trích xuất các nhánh sông chính theo ngưỡng diện tích lưu vực, loại bỏ rãnh nhỏ.
2. **Topographic & Ecological Analysis (Địa mạo & Sinh thái)**:
   - Độ dốc (Slope Gradient) & Chỉ số độ ẩm địa hình (Topographic Wetness Index - TWI).
   - Phân tầng sinh thái tự nhiên:
     - $Slope > 28^\circ$ hoặc viền vực đảo bay: Lộ đá trơ (Rock / Scree).
     - Vùng $TWI$ cao hoặc lòng sông/hồ: Nước, đầm lầy, sậy ven hồ (Water / Wetland / Reeds).
     - Vùng sườn đồi thoai thoải, độ ẩm vừa phải: Thảm cỏ xanh mướt (Meadow Grass).
     - Đỉnh gò khô ráo, đón nắng: Cụm hoa dại tự nhiên (Wildflower Knolls).
3. **Floating Island Perimeter & Abyss Carving**:
   - Tự động tạo dải vách dốc mềm mại ở chu vi đảo bay để hòa quyện vào biển mây (Cloud Sea).
4. **Export Engine Assets**:
   - Ghi file ảnh và C header sang `assets/maps/[prefix]/` để map plugin C nạp trực tiếp.

## 3. Cách Sử Dụng
```bash
python3 maps/pipeline/dem_pipeline.py <dem_path> --prefix song_quao --out-dir assets/maps/song_quao/ --max-dim 300.0 --height-range 16.0
```
