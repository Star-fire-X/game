#!/usr/bin/env python3
"""
地图渲染示例脚本（Python版本）

演示如何使用PNG和元数据JSON来渲染传奇2地图

依赖:
    pip install pillow numpy

用法:
    python render_map_with_metadata.py <map_json> <metadata_json> <output.png>

示例:
    python render_map_with_metadata.py map_3.json metadata_all.json output_3.png
"""

import json
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple

from PIL import Image, ImageDraw

# 常量
TILE_WIDTH = 48
TILE_HEIGHT = 32


class SpriteMetadata:
    """精灵元数据"""
    
    def __init__(self, data: dict):
        self.index = data.get('index', 0)
        self.filename = data.get('filename', '')
        self.width = data.get('width', 0)
        self.height = data.get('height', 0)
        self.offset_x = data.get('offset_x', 0)
        self.offset_y = data.get('offset_y', 0)
        self.valid = data.get('valid', False)
        self.png_exists = data.get('png_exists', False)


class MetadataLoader:
    """元数据加载器"""
    
    def __init__(self, metadata_path: str):
        with open(metadata_path, 'r', encoding='utf-8') as f:
            self.data = json.load(f)
        
        self.archives = self.data.get('archives', {})
    
    def get_sprite_metadata(self, archive_name: str, index: int) -> Optional[SpriteMetadata]:
        """获取精灵元数据"""
        if archive_name not in self.archives:
            return None
        
        archive = self.archives[archive_name]
        sprites = archive.get('sprites', [])
        
        # 查找指定索引
        for sprite_data in sprites:
            if sprite_data.get('index') == index:
                return SpriteMetadata(sprite_data)
        
        return None
    
    def get_png_path(self, archive_name: str, index: int) -> Optional[Path]:
        """获取PNG文件路径"""
        if archive_name not in self.archives:
            return None
        
        archive = self.archives[archive_name]
        png_dir = Path(archive.get('png_directory', ''))
        
        return png_dir / f"{index}.png"


class PngCache:
    """PNG图像缓存"""
    
    def __init__(self, metadata_loader: MetadataLoader):
        self.metadata = metadata_loader
        self.cache: Dict[Tuple[str, int], Image.Image] = {}
    
    def get_image(self, archive_name: str, index: int) -> Optional[Image.Image]:
        """获取PNG图像（带缓存）"""
        key = (archive_name, index)
        
        if key in self.cache:
            return self.cache[key]
        
        png_path = self.metadata.get_png_path(archive_name, index)
        if not png_path or not png_path.exists():
            return None
        
        try:
            img = Image.open(png_path).convert('RGBA')
            self.cache[key] = img
            return img
        except Exception as e:
            print(f"加载PNG失败 {png_path}: {e}")
            return None


class MapTile:
    """地图tile数据"""
    
    def __init__(self, data: dict):
        self.background = data.get('background', 0)
        self.middle = data.get('middle', 0)
        self.object = data.get('object', 0)
        self.door_index = data.get('door_index', 0)
        self.door_offset = data.get('door_offset', 0)
        self.anim_frame = data.get('anim_frame', 0)
        self.anim_tick = data.get('anim_tick', 0)
        self.area = data.get('area', 0)
        self.light = data.get('light', 0)
    
    def is_walkable(self) -> bool:
        """判断是否可行走"""
        return (self.light & 0x01) == 0


class MapData:
    """地图数据"""
    
    def __init__(self, json_path: str):
        with open(json_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        self.width = data.get('width', 0)
        self.height = data.get('height', 0)
        self.tiles = [MapTile(t) for t in data.get('tiles', [])]
    
    def get_tile(self, x: int, y: int) -> Optional[MapTile]:
        """获取指定坐标的tile"""
        if x < 0 or x >= self.width or y < 0 or y >= self.height:
            return None
        
        index = y * self.width + x
        if index >= len(self.tiles):
            return None
        
        return self.tiles[index]


class MapRenderer:
    """地图渲染器"""
    
    def __init__(self, map_data: MapData, png_cache: PngCache, 
                 metadata_loader: MetadataLoader):
        self.map_data = map_data
        self.png_cache = png_cache
        self.metadata = metadata_loader
    
    def render(self, output_path: str,
               render_background: bool = True,
               render_middle: bool = True,
               render_objects: bool = True,
               draw_grid: bool = False,
               draw_walkable: bool = False):
        """渲染地图到PNG文件"""
        
        # 创建画布
        canvas_width = self.map_data.width * TILE_WIDTH
        canvas_height = self.map_data.height * TILE_HEIGHT
        
        print(f"创建画布: {canvas_width}x{canvas_height}")
        canvas = Image.new('RGBA', (canvas_width, canvas_height), (0, 0, 0, 0))
        
        # 渲染背景层
        if render_background:
            print("渲染背景层...")
            self._render_layer(canvas, 'Tiles', 'background')
        
        # 渲染中间层
        if render_middle:
            print("渲染中间层...")
            self._render_layer(canvas, 'SmTiles', 'middle')
        
        # 渲染物件层
        if render_objects:
            print("渲染物件层...")
            self._render_layer(canvas, 'Objects', 'object')
        
        # 绘制网格
        if draw_grid:
            print("绘制网格...")
            self._draw_grid(canvas)
        
        # 绘制可行走性
        if draw_walkable:
            print("绘制可行走性...")
            self._draw_walkable(canvas)
        
        # 保存
        print(f"保存图像: {output_path}")
        canvas.save(output_path, 'PNG')
        print("完成！")
    
    def _render_layer(self, canvas: Image.Image, archive_name: str, 
                      tile_field: str):
        """渲染单个图层"""
        rendered_count = 0
        
        for y in range(self.map_data.height):
            for x in range(self.map_data.width):
                tile = self.map_data.get_tile(x, y)
                if not tile:
                    continue
                
                # 获取tile索引
                tile_index = getattr(tile, tile_field, 0)
                if tile_index == 0:
                    continue
                
                # 屏蔽高位标志
                tile_index &= 0x7FFF
                
                # 加载PNG图像
                tile_img = self.png_cache.get_image(archive_name, tile_index)
                if not tile_img:
                    continue
                
                # 获取偏移量
                sprite_meta = self.metadata.get_sprite_metadata(archive_name, tile_index)
                offset_x = sprite_meta.offset_x if sprite_meta else 0
                offset_y = sprite_meta.offset_y if sprite_meta else 0
                
                # 计算屏幕坐标
                screen_x = x * TILE_WIDTH + offset_x
                screen_y = y * TILE_HEIGHT + offset_y
                
                # 贴图（Alpha混合）
                canvas.paste(tile_img, (screen_x, screen_y), tile_img)
                rendered_count += 1
            
            # 进度显示
            if y % 10 == 0:
                print(f"  进度: {y}/{self.map_data.height}", end='\r')
        
        print(f"  完成: {rendered_count} tiles")
    
    def _draw_grid(self, canvas: Image.Image):
        """绘制网格线"""
        draw = ImageDraw.Draw(canvas, 'RGBA')
        
        grid_color = (128, 128, 128, 255)
        
        # 竖线
        for x in range(self.map_data.width + 1):
            px = x * TILE_WIDTH
            draw.line([(px, 0), (px, canvas.height)], fill=grid_color, width=1)
        
        # 横线
        for y in range(self.map_data.height + 1):
            py = y * TILE_HEIGHT
            draw.line([(0, py), (canvas.width, py)], fill=grid_color, width=1)
    
    def _draw_walkable(self, canvas: Image.Image):
        """绘制可行走性覆盖层"""
        overlay = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)
        
        for y in range(self.map_data.height):
            for x in range(self.map_data.width):
                tile = self.map_data.get_tile(x, y)
                if not tile:
                    continue
                
                walkable = tile.is_walkable()
                color = (0, 255, 0, 64) if walkable else (255, 0, 0, 64)
                
                px = x * TILE_WIDTH
                py = y * TILE_HEIGHT
                
                draw.rectangle(
                    [(px, py), (px + TILE_WIDTH, py + TILE_HEIGHT)],
                    fill=color
                )
        
        # 合成到画布
        canvas.alpha_composite(overlay)


def main():
    if len(sys.argv) < 4:
        print("用法: python render_map_with_metadata.py <map_json> <metadata_json> <output.png>")
        print("")
        print("示例:")
        print("  python render_map_with_metadata.py map_3.json metadata_all.json output_3.png")
        sys.exit(1)
    
    map_json_path = sys.argv[1]
    metadata_json_path = sys.argv[2]
    output_png_path = sys.argv[3]
    
    # 加载地图数据
    print(f"加载地图: {map_json_path}")
    map_data = MapData(map_json_path)
    print(f"地图尺寸: {map_data.width}x{map_data.height}")
    
    # 加载元数据
    print(f"加载元数据: {metadata_json_path}")
    metadata_loader = MetadataLoader(metadata_json_path)
    
    # 创建PNG缓存
    png_cache = PngCache(metadata_loader)
    
    # 创建渲染器
    renderer = MapRenderer(map_data, png_cache, metadata_loader)
    
    # 渲染地图
    renderer.render(
        output_png_path,
        render_background=True,
        render_middle=True,
        render_objects=True,
        draw_grid=False,      # 可选：--grid参数
        draw_walkable=False   # 可选：--walkable参数
    )


if __name__ == '__main__':
    main()
