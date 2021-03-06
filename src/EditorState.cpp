#include "EditorState.h"
#include "StartState.h"

#include "Common.h"

#include <stack>


const bool			SHOW_DEBUG_DEFAULT	= false;
const bool			HIDE_UI_DEFAULT		= false;

const float			SCROLL_SPEED		= 250.0f;

SDL_Surface*		MINI_MAP_SURFACE	= nullptr; // HACK!


std::stack<Point_2d> FLOOD_STACK;		// Stack used for contiguous flood fill.


std::map<EditState, string>	StateStringMap;			/**< EditState string table. */
std::map<int, EditState>	StateIntMap;			/**< EditState int table. */
std::map<EditState, Cell::TileLayer> StateToLayer;	/**< Translation table between a specific edit state and tile layer. */

bool layer_hidden(EditState _s, ToolBar& _t)
{
	switch (_s)
	{
	case STATE_BASE_TILE_INDEX:
		return !_t.show_bg();
	case STATE_BASE_DETAIL_TILE_INDEX:
		return !_t.show_bg_detail();
	case STATE_DETAIL_TILE_INDEX:
		return !_t.show_detail();
	case STATE_FOREGROUND_TILE_INDEX:
		return !_t.show_foreground();
	case STATE_TILE_COLLISION:
		return !_t.show_collision();
	default:
		return false;
	}
}


EditorState::EditorState(const string& mapPath):
	mMousePointer(nullptr),
	mPointer_Normal("sys/normal.png"),
	mPointer_Fill("sys/fill.png"),
	mPointer_Eraser("sys/eraser.png"),
	mLayerHidden("sys/layer_hidden.png"),
	mFont("fonts/ui-normal.png", 7, 9, 0),
	mLinkCell(nullptr),
	mMap(mapPath),
	mMapSavePath(mapPath),
	mEditState(STATE_BASE_TILE_INDEX),
	mPreviousEditState(mEditState),
	mDrawDebug(SHOW_DEBUG_DEFAULT),
	mLeftButtonDown(false),
	mRightButtonDown(false),
	mPlacingCollision(false),
	mHideUi(HIDE_UI_DEFAULT),
	mMapChanged(false),
	mReturnState(nullptr)
{}


EditorState::EditorState(const string& name, const string& mapPath, const string& tsetPath, int w, int h):
	mMousePointer(nullptr),
	mPointer_Normal("sys/normal.png"),
	mPointer_Fill("sys/fill.png"),
	mPointer_Eraser("sys/eraser.png"),
	mLayerHidden("sys/layer_hidden.png"),
	mFont("fonts/ui-normal.png", 7, 9, 0),
	mLinkCell(nullptr),
	mMap(name, tsetPath, w, h),
	mMapSavePath(mapPath),
	mPreviousEditState(STATE_BASE_TILE_INDEX),
	mDrawDebug(SHOW_DEBUG_DEFAULT),
	mLeftButtonDown(false),
	mRightButtonDown(false),
	mPlacingCollision(false),
	mHideUi(HIDE_UI_DEFAULT),
	mMapChanged(false),
	mReturnState(nullptr)
{}


EditorState::~EditorState()
{
	Utility<EventHandler>::get().keyUp().Disconnect(this, &EditorState::onKeyUp);
	Utility<EventHandler>::get().keyDown().Disconnect(this, &EditorState::onKeyDown);
	Utility<EventHandler>::get().mouseMotion().Disconnect(this, &EditorState::onMouseMove);
	Utility<EventHandler>::get().mouseButtonUp().Disconnect(this, &EditorState::onMouseUp);
	Utility<EventHandler>::get().mouseButtonDown().Disconnect(this, &EditorState::onMouseDown);
	Utility<EventHandler>::get().quit().Disconnect(this, &EditorState::onQuit);

	if (MINI_MAP_SURFACE)
	{
		SDL_FreeSurface(MINI_MAP_SURFACE);
		MINI_MAP_SURFACE = nullptr;
	}
}

/**
 * Initializes internal values and sets up event handlers.
 */
void EditorState::initialize()
{
	mReturnState = this;

	initUi();

	mMousePointer = &mPointer_Normal;

	// Fill tables
	fillTables();
	
	// Hook up event handlers
	Utility<EventHandler>::get().keyUp().Connect(this, &EditorState::onKeyUp);
	Utility<EventHandler>::get().keyDown().Connect(this, &EditorState::onKeyDown);
	Utility<EventHandler>::get().mouseMotion().Connect(this, &EditorState::onMouseMove);
	Utility<EventHandler>::get().mouseButtonUp().Connect(this, &EditorState::onMouseUp);
	Utility<EventHandler>::get().mouseButtonDown().Connect(this, &EditorState::onMouseDown);
	Utility<EventHandler>::get().quit().Connect(this, &EditorState::onQuit);

	mMap.viewport(Rectangle_2d(0, 32, Utility<Renderer>::get().width(), Utility<Renderer>::get().height() - 32));
}


void EditorState::initUi()
{
	// Tile Palette
	mTilePalette.tileset(&mMap.tileset());
	mTilePalette.font(mFont);

	// ToolBar
	mToolBar.map_name(mMap.name());
	mToolBar.toolbar_event().Connect(this, &EditorState::toolbar_event);

	// Mini Map
	mMiniMap.font(&mFont);
	mMiniMap.map(&mMap);
	mMiniMap.hidden(!mToolBar.show_minimap());

	// Link Edit UI
	mBtnLinkOkay.font(mFont);
	mBtnLinkOkay.size(50, 25);
	mBtnLinkOkay.position(10, 160);
	mBtnLinkOkay.text("Okay");
	mBtnLinkOkay.click().Connect(this, &EditorState::button_MapLinkOkay_Click);
	mBtnLinkOkay.visible(false);

	mBtnLinkCancel.font(mFont);
	mBtnLinkCancel.size(50, 25);
	mBtnLinkCancel.position(75, 160);
	mBtnLinkCancel.text("Cancel");
	mBtnLinkCancel.click().Connect(this, &EditorState::button_MapLinkCancel_Click);
	mBtnLinkCancel.visible(false);

	mTxtLinkDestination.font(mFont);
	mTxtLinkDestination.width(300);
	mTxtLinkDestination.position(10, 100);
	mTxtLinkDestination.border(TextField::ALWAYS);
	mTxtLinkDestination.visible(false);

	mTxtLinkDestX.font(mFont);
	mTxtLinkDestX.width(100);
	mTxtLinkDestX.position(10, 130);
	mTxtLinkDestX.text("0");
	mTxtLinkDestX.border(TextField::ALWAYS);
	mTxtLinkDestX.visible(false);

	mTxtLinkDestY.font(mFont);
	mTxtLinkDestY.width(100);
	mTxtLinkDestY.position(150, 130);
	mTxtLinkDestY.text("0");
	mTxtLinkDestY.border(TextField::ALWAYS);
	mTxtLinkDestY.visible(false);
}


void EditorState::fillTables()
{
	fillEditStateStringTable();
	fillStateToLayerTable();
}


/**
 * Fills a table with Ints and maps them to EditStates.
 */
void EditorState::fillEditStateStringTable()
{
	StateStringMap[STATE_BASE_TILE_INDEX]			= "Base Layer Editing";
	StateStringMap[STATE_BASE_DETAIL_TILE_INDEX]	= "Base Detail Layer Editing";
	StateStringMap[STATE_DETAIL_TILE_INDEX]			= "Detail Layer Editing";
	StateStringMap[STATE_FOREGROUND_TILE_INDEX]		= "Foreground Layer Editing";
	StateStringMap[STATE_TILE_COLLISION]			= "Collision Layer Editing";
	StateStringMap[STATE_MAP_LINK_EDIT]				= "Map Link Editing";
}


/**
 * Fills a table to use as an easy translation from layer edit state to cell layer.
 */
void EditorState::fillStateToLayerTable()
{
	StateToLayer[STATE_BASE_TILE_INDEX]			= Cell::LAYER_BASE;
	StateToLayer[STATE_BASE_DETAIL_TILE_INDEX]	= Cell::LAYER_BASE_DETAIL;
	StateToLayer[STATE_DETAIL_TILE_INDEX]		= Cell::LAYER_DETAIL;
	StateToLayer[STATE_FOREGROUND_TILE_INDEX]	= Cell::LAYER_FOREGROUND;
}

/**
 * Handler link okay button click.
 * 
 * \warning	Does absolutely no error checking at all
 *			so make sure values are good.
 */
void EditorState::button_MapLinkOkay_Click()
{
	if(mLinkCell)
	{
		mLinkCell->link(mTxtLinkDestination.text());
		mLinkCell->link_destination(Point_2d(stringToInt(mTxtLinkDestX.text()), stringToInt(mTxtLinkDestY.text())));
	}

	mBtnLinkOkay.visible(false);
	mBtnLinkCancel.visible(false);
	mTxtLinkDestination.visible(false);
	mTxtLinkDestX.visible(false);
	mTxtLinkDestY.visible(false);

	mLinkCell = NULL;

	restorePreviousState();
}


/**
 * Handler for the button's Click event.
 */
void EditorState::button_MapLinkCancel_Click()
{
	restorePreviousState();

	mBtnLinkOkay.visible(false);
	mBtnLinkCancel.visible(false);
	mTxtLinkDestination.visible(false);
	mTxtLinkDestX.visible(false);
	mTxtLinkDestY.visible(false);
}


/**
 * 
 */
State* EditorState::update()
{
	Renderer& r = Utility<Renderer>::get();
	r.clearScreen(COLOR_MAGENTA);

	updateScroll();
	updateSelector();


	if(mHideUi)
		return mReturnState;

	if(mDrawDebug)
		debug();

	if(mEditState == STATE_MAP_LINK_EDIT)
	{
		r.drawBoxFilled(0, 0, r.width(), r.height(), 0, 0, 0, 65);
		r.drawBox(mCellInspectRect, 255, 255, 0);
	}

	updateUI();

	r.drawTextShadow(mFont, "Map File: " + mMapSavePath, r.screenCenterX() - (mFont.width("Map File: " + mMapSavePath) / 2), r.height() - (mFont.height() + 2), 1, 255, 255, 255, 0, 0, 0);

	r.drawImage(*mMousePointer, mMouseCoords.x(), mMouseCoords.y());
	if (layer_hidden(mEditState, mToolBar))
		r.drawImage(mLayerHidden, mMouseCoords.x(), mMouseCoords.y() + 34, 1.0f, 255, 255, 0, 255);

	return mReturnState;
}


void EditorState::updateUI()
{
	//instructions();
	Renderer& r = Utility<Renderer>::get();

	mToolBar.update();
	mMiniMap.update();

	r.drawTextShadow(mFont, string_format("World Tile: %i, %i", static_cast<int>((mMouseCoords.x() + mMap.cameraPosition().x()) / mMap.tileset().width()), static_cast<int>((mMouseCoords.y() + mMap.cameraPosition().y() - mMap.viewport().y()) / mMap.tileset().height())), 5, r.height() - 28, 1, 255, 255, 255, 0, 0, 0);
	r.drawTextShadow(mFont, string_format("World Fine: %i, %i", static_cast<int>(mMouseCoords.x() + mMap.cameraPosition().x() - mMap.viewport().x()), static_cast<int>(mMouseCoords.y() + mMap.cameraPosition().y() - mMap.viewport().y())), 5, r.height() - 15, 1, 255, 255, 255, 0, 0, 0);

	mTilePalette.update();

	mBtnLinkOkay.update();
	mBtnLinkCancel.update();

	mTxtLinkDestination.update();
	mTxtLinkDestX.update();
	mTxtLinkDestY.update();
}


/**
 * Updates the map scrolling.
 * 
 * Also updates the current tick time.
 */
void EditorState::updateScroll()
{
	mMap.update();
	if(!mHideUi)
	{
		mSelectorRect = mMap.injectMousePosition(mMouseCoords);
	}

	float delta = (mTimer.delta() / 1000.0f);
	mMap.moveCamera(static_cast<float>(mScrollVector.x()) * delta, static_cast<float>(mScrollVector.y()) * delta);
}


/**
 * Draws the tile selector based on the pattern selected in the TilePalette.
 */
void EditorState::updateSelector()
{
	// Don't draw selector if the UI is hidden.
	if(mHideUi || mMouseCoords.y() < 32)
		return;

	if (mTilePalette.responding_to_events() || mMiniMap.responding_to_events())
		return;

	Renderer& r = Utility<Renderer>::get();

	// Draw Tile Selector
	int offsetX = 0, offsetY = 0;
	
	const Pattern* p = &mTilePalette.pattern();
	if(mEditState == STATE_TILE_COLLISION || mToolBar.erase()) p = &mToolBar.brush();

	for(int row = p->height(); row > 0; row--)
	{
		for(int col = p->width(); col > 0; col--)
		{
			r.drawBox(mSelectorRect.x() - offsetX + mMap.viewport().x(), mSelectorRect.y() - offsetY + mMap.viewport().y(), mSelectorRect.w(), mSelectorRect.h(), 255, 255, 255);
			offsetX += 32;
		}
		offsetX = 0;
		offsetY += 32;
	}
}


/**
 * Handles KeyDown events.
 */
void EditorState::onKeyDown(KeyCode key, KeyModifier mod, bool repeat)
{
	if(repeat)
		return;

	if(mEditState == STATE_MAP_LINK_EDIT)
	{
		return;
	}

	switch(key)
	{
		case KEY_ESCAPE:
			mReturnState = new StartState();
			break;

		case KEY_LEFT:
			mScrollVector.x() = -SCROLL_SPEED;
			break;

		case KEY_RIGHT:
			mScrollVector.x() = SCROLL_SPEED;
			break;

		case KEY_UP:
			mScrollVector.y() = -SCROLL_SPEED;
			break;

		case KEY_DOWN:
			mScrollVector.y() = SCROLL_SPEED;
			break;

		case KEY_F1:
			mDrawDebug = !mDrawDebug;
			mMap.showLinks(mDrawDebug);
			break;

		// Stealing this for dumping the minimap for now
		case KEY_F2:
			SDL_SaveBMP(MINI_MAP_SURFACE, "minimap.bmp");
			break;

		case KEY_F3:
			mBtnLinkOkay.visible(true);
			mBtnLinkCancel.visible(true);
			mTxtLinkDestination.visible(true);
			mTxtLinkDestX.visible(true);
			mTxtLinkDestY.visible(true);
			mLinkCell = &mMap.getCell(mMouseCoords);
			mCellInspectRect = mMap.injectMousePosition(mMouseCoords);
			mTxtLinkDestination.text(mLinkCell->link());
			mTxtLinkDestX.text(string_format("%i", mLinkCell->link_destination().x()));
			mTxtLinkDestY.text(string_format("%i", mLinkCell->link_destination().y()));
			setState(STATE_MAP_LINK_EDIT);
			break;

		case KEY_F10:
			mHideUi = !mHideUi;
			break;

		case KEY_z:
			if(KeyTranslator::control(mod))
			{
				if(!mFieldUndo.empty())
				{
					mMap.field(mFieldUndo);
					mMiniMap.update_minimap();
				}
			}

			break;

		default:
			break;
	}
}


/**
 * Handles KeyUp events.
 */
void EditorState::onKeyUp(KeyCode key, KeyModifier mod)
{
	if(mEditState == STATE_MAP_LINK_EDIT)
	{
		return;
	}

	if(key == KEY_LEFT)
		mScrollVector.x() = 0.0f;
	else if(key == KEY_RIGHT)
		mScrollVector.x() = 0.0f;
	else if(key == KEY_UP)
		mScrollVector.y() = 0.0f;
	else if(key == KEY_DOWN)
		mScrollVector.y() = 0.0f;
}


/**
 *
 */
void EditorState::onQuit()
{
	mReturnState = NULL;
}


/**
 *
 */
void EditorState::onMouseMove(int x, int y, int relX, int relY)
{
	if(mRightButtonDown && mEditState != STATE_MAP_LINK_EDIT)
	{
		mMap.moveCamera(relX, relY);
		return;
	}

	mMouseCoords(x, y);

	if(mLeftButtonDown)
	{
		// Avoid modifying tiles if we're in the 'toolbar area'
		if (y < 32 || mToolBar.flood() || mTilePalette.responding_to_events() || mMiniMap.responding_to_events())
			return;

		if(mEditState == STATE_TILE_COLLISION)
			pattern_collision();
		else
			changeTileTexture();
	}
}


/**
 * Handles MouseDown events.
 */
void EditorState::onMouseDown(MouseButton button, int x, int y)
{
	Utility<EventHandler>::get().grabMouse();

	// Left Mouse Button
	if(button == BUTTON_LEFT)
	{
		mLeftButtonDown = true;
		handleLeftButtonDown(x, y);
	}
	else if(button == BUTTON_RIGHT)
	{
		mRightButtonDown = true;
		mSavedMouseCoords = mMouseCoords;
		Utility<EventHandler>::get().mouseRelativeMode(true);
	}
}


/**
 * Handles MouseUp events
 */
void EditorState::onMouseUp(MouseButton button, int x, int y)
{
	if(button == BUTTON_LEFT)
	{
		mLeftButtonDown = false;
		if(mEditState == STATE_MAP_LINK_EDIT)
		{
		}
		else if(mEditState == STATE_BASE_TILE_INDEX || mEditState == STATE_BASE_DETAIL_TILE_INDEX || mEditState == STATE_DETAIL_TILE_INDEX || mEditState == STATE_FOREGROUND_TILE_INDEX)
		{
			if(mMapChanged)
			{
				mMiniMap.update_minimap();
				mMapChanged = false;
			}
		}
	}
	else if(button == BUTTON_RIGHT)
	{
		mRightButtonDown = false;
		Utility<EventHandler>::get().mouseRelativeMode(false);
		Utility<EventHandler>::get().warpMouse(mSavedMouseCoords.x(), mSavedMouseCoords.y()); // a bit hacky but does the job
	}

	Utility<EventHandler>::get().releaseMouse();
}


/**
 * Handles left mouse button down.
 */
void EditorState::handleLeftButtonDown(int x, int y)
{
	Point_2d pt(x, y);

	// Hate the look of this but it effectively condenses the ignore checks.
	if (y < 32 ||
		(mToolBar.flood() && isPointInRect(pt, mToolBar.flood_tool_extended_area())) ||
		mEditState == STATE_MAP_LINK_EDIT ||
		isPointInRect(pt, mTilePalette.rect()) ||
		isPointInRect(pt, mMiniMap.rect()) ||
		isPointInRect(pt, mTilePalette.rect()))
		return;


	Cell& cell = mMap.getCell(mMouseCoords);

	if(mEditState == STATE_TILE_COLLISION)
	{
		saveUndo();
		cell.blocked(!cell.blocked());
		mPlacingCollision = cell.blocked();
		pattern_collision();
	}
	else
	{
		saveUndo();
		changeTileTexture();
	}
}


/**
 * Changes the tile texture index of the highlighted Cell.
 * 
 * \note	Will not modify tile texture index unless in the correct edit mode.
 */
void EditorState::changeTileTexture()
{
	if (StateToLayer.find(mEditState) == StateToLayer.end())
		throw Exception(0, "Bad State", "EditorState::changeTileTExture() called with an invalid state.");

	if (layer_hidden(mEditState, mToolBar))
		return;

	if (mToolBar.flood())
	{
		if (mToolBar.flood_contiguous())
			patternFill_Contig(StateToLayer[mEditState], mMap.getGridCoords(mMouseCoords), mMap.getCell(mMouseCoords).index(StateToLayer[mEditState]));
		else
			patternFill(StateToLayer[mEditState]);
	}
	else if (mToolBar.pencil())
		pattern(StateToLayer[mEditState]);
	else if (mToolBar.erase())
		pattern(StateToLayer[mEditState], -1);
	else // Defined this way to avoid forgetting to add possible new tools to the check.
		return;

	mMapChanged = true;
	return;
}


/**
 * Fills a given cell layer with a pattern.
 */
void EditorState::patternFill(Cell::TileLayer layer)
{
	const Pattern& p = mTilePalette.pattern();

	for(int row = 0; row < mMap.height(); row++)
		for(int col = 0; col < mMap.width(); col++)
			mMap.getCellByGridCoords(col, row).index(layer, p.value(col % p.width(), row % p.height()));
}


/**
 * Fills a contiguous area in a given layer with a pattern.
 */
void EditorState::patternFill_Contig(Cell::TileLayer layer, const Point_2d& _pt, int seed_index)
{
	const Pattern& _pCheck = mTilePalette.pattern();
	if (seed_index == _pCheck.value(_pt.x() % _pCheck.width(), _pt.y() % _pCheck.height()))
		return;

	while (!FLOOD_STACK.empty())
		FLOOD_STACK.pop();

	static const vector<int> dX = { 0, 1, 0, -1 }; // Neighbor Coords
	static const vector<int> dY = { -1, 0, 1, 0 }; // Neighbor Coords

	FLOOD_STACK.push(_pt);
	
	while(!FLOOD_STACK.empty())
	{
		const Point_2d _pt_top = FLOOD_STACK.top();
		FLOOD_STACK.pop();

		const Pattern& p = mTilePalette.pattern();
		mMap.getCellByGridCoords(_pt_top).index(layer, p.value(_pt_top.x() % p.width(), _pt_top.y() % p.height()));

		for (int i = 0; i < 4; i++)
		{
			Point_2d coord(_pt_top.x() + dX[i], _pt_top.y() + dY[i]);
			if (coord.x() >= 0 && coord.x() < mMap.width() && coord.y() >= 0 && coord.y() < mMap.height() && mMap.getCellByGridCoords(coord).index(layer) == seed_index)
				FLOOD_STACK.push(coord);
		}
	}

}


/**
* If value < 0, ignores the pattern values and writes -1 instead.
*/
void EditorState::pattern(Cell::TileLayer layer, int value)
{
	const Pattern* _p = &mTilePalette.pattern();
	if (value < 0)
		_p = &mToolBar.brush();


	Point_2d& _pt = mMap.getGridCoords(mMouseCoords);

	for (int row = 0; row < _p->height(); row++)
	{
		for (int col = 0; col < _p->width(); col++)
		{
			int index = _p->value(col, row);
			int x = _pt.x() - ((_p->width() - 1) - col);
			int y = _pt.y() - ((_p->height() - 1) - row);

			if (x >= 0 && y >= 0)
			{
				if (value >= 0) mMap.getCellByGridCoords(x, y).index(layer, _p->value(col, row));
				else mMap.getCellByGridCoords(x, y).index(layer, -1);
			}
		}
	}
}


/**
 * Performs a pattern edit on collision layer.
 * 
 * \todo	Have this check the ToolBar for a pattern size
 * 			instead of using a pattern from the TilePalette.
 */
void EditorState::pattern_collision()
{
	const Pattern& _p = mToolBar.brush();
	Point_2d& _pt = mMap.getGridCoords(mMouseCoords);

	for (int row = 0; row < _p.height(); row++)
	{
		for (int col = 0; col < _p.width(); col++)
		{
			int x = _pt.x() - ((_p.width() - 1) - col);
			int y = _pt.y() - ((_p.height() - 1) - row);

			if (x >= 0 && y >= 0)
				mMap.getCellByGridCoords(x, y).blocked(mPlacingCollision);
		}
	}
}


void EditorState::toolbar_event(ToolBar::ToolBarAction _act)
{
	switch (_act)
	{
	case ToolBar::TOOLBAR_SAVE:
		saveMap();
		break;
	case ToolBar::TOOLBAR_LAYER_BG_EDIT:
		mEditState = STATE_BASE_TILE_INDEX;
		break;
	case ToolBar::TOOLBAR_LAYER_BG_DETAIL_EDIT:
		mEditState = STATE_BASE_DETAIL_TILE_INDEX;
		break;
	case ToolBar::TOOLBAR_LAYER_DETAIL_EDIT:
		mEditState = STATE_DETAIL_TILE_INDEX;
		break;
	case ToolBar::TOOLBAR_LAYER_FOREGROUND_EDIT:
		mEditState = STATE_FOREGROUND_TILE_INDEX;
		break;
	case ToolBar::TOOLBAR_LAYER_COLLISION_EDIT:
		mEditState = STATE_TILE_COLLISION;
		break;
	case ToolBar::TOOLBAR_LAYER_BG_TOGGLE:
	case ToolBar::TOOLBAR_LAYER_BG_DETAIL_TOGGLE:
	case ToolBar::TOOLBAR_LAYER_DETAIL_TOGGLE:
	case ToolBar::TOOLBAR_LAYER_FOREGROUND_TOGGLE:
	case ToolBar::TOOLBAR_LAYER_COLLISION_TOGGLE:
		mMap.drawBg(mToolBar.show_bg());
		mMap.drawBgDetail(mToolBar.show_bg_detail());
		mMap.drawDetail(mToolBar.show_detail());
		mMap.drawForeground(mToolBar.show_foreground());
		mMap.drawCollision(mToolBar.show_collision());
		if (mToolBar.show_collision()) mTilePalette.reset();
		break;
	case ToolBar::TOOLBAR_MINIMAP_TOGGLE:
		mMiniMap.hidden(!mToolBar.show_minimap());
		break;
	case ToolBar::TOOLBAR_TILE_PALETTE_TOGGLE:
		mTilePalette.hidden(!mToolBar.show_tilepalette());
		break;

	case ToolBar::TOOLBAR_TOOL_PENCIL:
		mMousePointer = &mPointer_Normal;
		break;
	case ToolBar::TOOLBAR_TOOL_FILL:
		mMousePointer = &mPointer_Fill;
		break;
	case ToolBar::TOOLBAR_TOOL_ERASER:
		mMousePointer = &mPointer_Eraser;
		break;
	default:
		break;
	}
}


/**
 * Saves a map.
 * 
 * Displays the File Save Path entry screen if the map's save path is empty.
 */
void EditorState::saveMap()
{
	Filesystem& f = Utility<Filesystem>::get();
	if (!f.exists("maps"))
		f.makeDirectory("maps");

	mMap.name(mToolBar.map_name());
	mMap.save(mMapSavePath);
}


/**
 * Saves and undo level.
 */
void EditorState::saveUndo()
{
	mFieldUndo = mMap.field();
}


/**
 * Sets the current state and saves the previous state.
 */
void EditorState::setState(EditState state)
{
	if(mEditState == state)
		return;

	mPreviousEditState = mEditState;
	mEditState = state;
}


/**
 * Restores the previous state.
 */
void EditorState::restorePreviousState()
{
	EditState state = mEditState;
	mEditState = mPreviousEditState;
	mPreviousEditState = state;
}


/**
 * Draws debug information.
 */
void EditorState::debug()
{
	Renderer& r = Utility<Renderer>::get();

	// Cell coords pointed at by mouse.
	std::stringstream ss;
	Point_2d pt = mMap.getGridCoords(mMouseCoords);
	ss << "Cell Coords: " << pt.x() << ", " << pt.y();
	r.drawTextShadow(mFont, ss.str(), 4, 100, 1, 255, 255, 255, 0, 0, 0);


	// Edit State
	ss.str("");
	ss.str("Edit State: " + StateStringMap[mEditState]);
	r.drawTextShadow(mFont, ss.str(), 4, 115, 1, 255, 255, 255, 0, 0, 0);

	// Tile Index
	Cell& cell = mMap.getCell(mMouseCoords);

	ss.str("");
	ss << "Base: " << cell.index(Cell::LAYER_BASE);
	r.drawTextShadow(mFont, ss.str(), 4, 145, 1, 255, 255, 255, 0, 0, 0);

	ss.str("");
	ss << "Base Detail: " << cell.index(Cell::LAYER_BASE_DETAIL);
	r.drawTextShadow(mFont, ss.str(), 4, 160, 1, 255, 255, 255, 0, 0, 0);

	ss.str("");
	ss << "Detail: " << cell.index(Cell::LAYER_DETAIL);
	r.drawTextShadow(mFont, ss.str(), 4, 175, 1, 255, 255, 255, 0, 0, 0);

	ss.str("");
	ss << "Foreground: " << cell.index(Cell::LAYER_FOREGROUND);
	r.drawTextShadow(mFont, ss.str(), 4, 190, 1, 255, 255, 255, 0, 0, 0);

	ss.str("");
	cell.blocked() ? ss << "Blocked: true" : ss << "Blocked: false";
	r.drawTextShadow(mFont, ss.str(), 4, 205, 1, 255, 255, 255, 0, 0, 0);

	ss.str("");
	ss << "Link: " << cell.link();
	r.drawTextShadow(mFont, ss.str(), 4, 235, 1, 255, 255, 255, 0, 0, 0);

	ss.str("");
	ss << "Destination: " << cell.link_destination().x() << ", " << cell.link_destination().y();
	r.drawTextShadow(mFont, ss.str(), 4, 250, 1, 255, 255, 255, 0, 0, 0);

	mMap.tileset().drawTileColorPalette(r.width() - 32, 50, 4, 6);
}


/**
 * Draws a key mapping string.
 */
void EditorState::instructions()
{
	string str1 = "F1: Show/Hide Debug | F3: Map Link | F4: Save | F5: BG Detail | F6: Detail | F7: Foreground | F10: Hide/Show UI";
	Utility<Renderer>::get().drawTextShadow(mFont, str1, Utility<Renderer>::get().width() - mFont.width(str1) - 4, 4, 1, 255, 255, 255, 0, 0, 0);
}

