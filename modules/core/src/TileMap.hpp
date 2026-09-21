#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! A tilemap that cannot be read, or that was asked something impossible
    class TileMapError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    using TileId = std::uint16_t;

    //! Nothing in this cell
    constexpr TileId EMPTY_TILE = 0;

    //! One grid of tiles, drawn or used as a whole
    struct TileLayer
    {
        std::string          name;
        bool                 visible = true;
        std::vector<TileId>  tiles;   //!< width * height, row after row
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A level made of tiles: a size, layers of tile numbers, and which
     *         tiles are solid
     *
     * Only data: nothing here draws, and a server can load one to know where
     * the walls are. A tile is a number; 0 is "nothing", the others say which
     * picture of a tileset to draw (the client's Tileset).
     *
     * As a file:
     *
     *     # a comment
     *     kuge-tilemap 1
     *     size 8 3                 # in tiles
     *     tilesize 16              # in pixels
     *     solid 1 2                # the tiles that stop bodies
     *     layer ground
     *     1 1 1 1 1 1 1 1          # one row per line, numbers apart by spaces or commas
     *     0 0 0 0 0 0 0 0
     *     2 2 0 0 0 0 2 2
     *     layer sky hidden         # "hidden": present, but not drawn
     *     0 0 0 0 0 0 0 0
     *     0 0 0 0 0 0 0 0
     *     0 0 0 0 0 0 0 0
     */
    ////////////////////////////////////////////////////////////////////////////
    class TileMap
    {
        public:
            TileMap(void) = default;

            //! @throw TileMapError if a size is not positive
            TileMap(int width, int height, float tileSize);

            int   width(void) const noexcept { return m_width; }
            int   height(void) const noexcept { return m_height; }
            float tileSize(void) const noexcept { return m_tileSize; }

            //! An empty layer, on top of the others. The reference stays valid
            //! when other layers are added.
            //! @throw TileMapError if the name is taken or empty
            TileLayer& addLayer(std::string name);

            std::deque<TileLayer>&       layers(void) noexcept { return m_layers; }
            const std::deque<TileLayer>& layers(void) const noexcept { return m_layers; }

            TileLayer*       findLayer(std::string_view name) noexcept;
            const TileLayer* findLayer(std::string_view name) const noexcept;

            //! The tile at a place, EMPTY_TILE outside the map
            TileId tileAt(const TileLayer& layer, int x, int y) const noexcept
            {
                return inside(x, y) ? layer.tiles[index(x, y)] : EMPTY_TILE;
            }

            //! Outside the map: ignored
            void setTile(TileLayer& layer, int x, int y, TileId tile) noexcept
            {
                if (inside(x, y)) {
                    layer.tiles[index(x, y)] = tile;
                }
            }

            bool inside(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < m_width && y < m_height; }

            void setSolid(TileId tile, bool solid = true);
            bool isSolid(TileId tile) const { return tile != EMPTY_TILE && m_solid.count(tile) != 0; }
            const std::set<TileId>& solidTiles(void) const noexcept { return m_solid; }

            //! In the format above (parse(toString()) gives the same map)
            std::string toString(void) const;

            //! @throw TileMapError, with the number of the line that is wrong
            static TileMap parse(std::string_view text);

            //! @throw TileMapError
            static TileMap load(const std::filesystem::path& path);

            void save(const std::filesystem::path& path) const;

        private:
            std::size_t index(int x, int y) const noexcept
            {
                return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
            }

            int                     m_width    = 0;
            int                     m_height   = 0;
            float                   m_tileSize = 16.0f;
            std::deque<TileLayer>   m_layers;   // not a vector: addLayer() gives references
            std::set<TileId>        m_solid;
    };

}
