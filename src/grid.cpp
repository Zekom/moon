/*
 * grid.cpp: canvas definitions.
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2008 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#include <config.h>

#include <math.h>

#include "brush.h"
#include "rect.h"
#include "canvas.h"
#include "grid.h"
#include "runtime.h"
#include "namescope.h"
#include "collection.h"

Grid::Grid ()
{
	SetValue (Grid::ColumnDefinitionsProperty, Value::CreateUnref (new ColumnDefinitionCollection ()));
	SetValue (Grid::RowDefinitionsProperty, Value::CreateUnref (new RowDefinitionCollection ()));
}

void
Grid::OnPropertyChanged (PropertyChangedEventArgs *args)
{
	if (args->property->GetOwnerType() != Type::GRID) {
		Panel::OnPropertyChanged (args);
		return;
	}

	if (args->property == Grid::ShowGridLinesProperty){
		Invalidate ();
	}

	InvalidateMeasure ();

	NotifyListenersOfPropertyChange (args);
}

void
Grid::OnSubPropertyChanged (DependencyProperty *prop, DependencyObject *obj, PropertyChangedEventArgs *subobj_args)
{
	if (subobj_args->property == Grid::ColumnProperty
	    || subobj_args->property == Grid::RowProperty
	    || subobj_args->property == Grid::ColumnSpanProperty
	    || subobj_args->property == Grid::RowSpanProperty) {
		InvalidateMeasure ();
	}
	
	Panel::OnSubPropertyChanged (prop, obj, subobj_args);
}

void
Grid::OnCollectionChanged (Collection *col, CollectionChangedEventArgs *args)
{
	if (col == GetColumnDefinitions () ||
	    col == GetRowDefinitions ()) {

	} else {
		Panel::OnCollectionChanged (col, args);
	}
	
	InvalidateMeasure ();
}

Size
Grid::MeasureOverride (Size availableSize)
{
	Size results (0, 0);

	ColumnDefinitionCollection *columns = GetColumnDefinitions ();
	RowDefinitionCollection *rows = GetRowDefinitions ();

	int col_count = columns->GetCount ();
	int row_count = rows->GetCount ();

	if (col_count == 0) {
		columns = new ColumnDefinitionCollection ();
		columns->Add (new ColumnDefinition ());
		col_count = 1;
	}

	if (row_count == 0) {
		rows = new RowDefinitionCollection ();
		rows->Add (new RowDefinition ());
		row_count = 1;
	}

	for (int i = 0; i < row_count; i ++) {
		RowDefinition *rowdef = rows->GetValueAt (i)->AsRowDefinition ();
		GridLength* height = rowdef->GetHeight();
		
		rowdef->SetActualHeight (0.0);

		if (height->type == GridUnitTypePixel)
			rowdef->SetActualHeight (height->val);
	}

	for (int i = 0; i < col_count; i ++) {
		ColumnDefinition *coldef = columns->GetValueAt (i)->AsColumnDefinition ();
		GridLength* width = coldef->GetWidth();

		coldef->SetActualWidth (0.0);

		if (width->type == GridUnitTypePixel)
			coldef->SetActualWidth (width->val);
	}

	UIElementCollection *children = GetChildren ();
	for (int i = 0; i < children->GetCount(); i ++) {
		UIElement *child = children->GetValueAt(i)->AsUIElement ();
		gint32 col, row;
		gint32 colspan, rowspan;

		col = Grid::GetColumn (child);
		row = Grid::GetRow (child);
		colspan = Grid::GetColumnSpan (child);
		rowspan = Grid::GetRowSpan (child);

		Size child_size = Size (0,0);
		Size min_size = Size (0,0);
		Size max_size = Size (0,0); 
		
		for (int r = row; r < MIN (row + rowspan, row_count); r++) {
			RowDefinition *rowdef = rows->GetValueAt (r)->AsRowDefinition ();

			if (rowdef->GetHeight ()->type == GridUnitTypePixel)
			        child_size.height += rowdef->GetActualHeight ();
			else
				child_size.height += INFINITY;

			min_size.height += rowdef->GetMinHeight ();
			max_size.height += rowdef->GetMaxHeight ();
		}

		for (int c = col; c < MIN (col + colspan, col_count); c++) {
			ColumnDefinition *coldef = columns->GetValueAt (c)->AsColumnDefinition ();

			if (coldef->GetWidth ()->type == GridUnitTypePixel)
				child_size.width += coldef->GetActualWidth ();
			else
				child_size.width += INFINITY;

			min_size.width += coldef->GetMinWidth ();
			max_size.width += coldef->GetMaxWidth ();
		}
		
		
		child_size = child_size.Min (max_size);
		child_size = child_size.Max (min_size);

		child->Measure (child_size);
		child_size = child->GetDesiredSize();
		
		child_size = child_size.Min (max_size);
		child_size = child_size.Max (min_size);

		double remaining_width = child_size.width;
		for (int c = col; c < MIN (col + colspan, col_count); c++){
			ColumnDefinition *coldef = columns->GetValueAt (c)->AsColumnDefinition ();
			if (!coldef)
				break; // XXX what to do if col + colspan is more than the number of columns?
			
			if (coldef->GetWidth ()->type != GridUnitTypePixel) {
				coldef->SetActualWidth (MAX(coldef->GetActualWidth (), remaining_width));
				break;
			} else {
				remaining_width -= coldef->GetActualWidth ();
			}
		}

		double remaining_height = child_size.height;
		for (int r = row; r < MIN (row + rowspan, row_count); r++){
			RowDefinition *rowdef = rows->GetValueAt (r)->AsRowDefinition ();
			if (!rowdef)
				break; // XXX what to do if row + rowspan is more than the number of rows?
			
			if (rowdef->GetHeight ()->type != GridUnitTypePixel) {
				rowdef->SetActualHeight (MAX (rowdef->GetActualHeight (), remaining_height));
				break;
			} else  {
				remaining_height -= rowdef->GetActualHeight ();
			}
		}
	}

	Size grid_size;
	for (int r = 0; r < row_count; r ++) {
		grid_size.height += rows->GetValueAt (r)->AsRowDefinition ()->GetActualHeight ();
	}

	for (int c = 0; c < col_count; c ++) {
		grid_size.width += columns->GetValueAt (c)->AsColumnDefinition ()->GetActualWidth ();
	}

	results = results.Max (grid_size);

	// now choose whichever is smaller, our chosen size or the availableSize.
	return results;
}

Size
Grid::ArrangeOverride (Size finalSize)
{
	ColumnDefinitionCollection *columns = GetColumnDefinitions ();
	RowDefinitionCollection *rows = GetRowDefinitions ();

	int col_count = columns->GetCount ();
	int row_count = rows->GetCount ();

	if (col_count == 0) {
		columns = new ColumnDefinitionCollection ();
		columns->Add (new ColumnDefinition ());
		col_count = 1;
	}

	if (row_count == 0) {
		rows = new RowDefinitionCollection ();
		rows->Add (new RowDefinition ());
		row_count = 1;
	}

	for (int i = 0; i < col_count; i++)
		columns->GetValueAt (i)->AsColumnDefinition ()->SetActualWidth (0.0);

	for (int i = 0; i < row_count; i++)
		rows->GetValueAt (i)->AsRowDefinition ()->SetActualHeight (0.0);

	Size remaining = finalSize;

	VisualTreeWalker walker = VisualTreeWalker (this);
	while (UIElement *child = walker.Step ()) {
		gint32 col = Grid::GetColumn (child);
		gint32 row = Grid::GetRow (child);
		//gint32 colspan = Grid::GetColumnSpan (child);
		//gint32 rowspan = Grid::GetRowSpan (child);
		
		Size desired = child->GetDesiredSize ();
		if (col < col_count) {
			ColumnDefinition *coldef = columns->GetValueAt (col)->AsColumnDefinition ();

			if (coldef->GetWidth ()->type == GridUnitTypeAuto)
				coldef->SetActualWidth (MAX (coldef->GetActualWidth (), desired.width));

		}

		if (row < row_count) {
			RowDefinition *rowdef = rows->GetValueAt (row)->AsRowDefinition ();
			
			if (rowdef->GetHeight ()->type == GridUnitTypeAuto)
				rowdef->SetActualHeight (MAX (rowdef->GetActualHeight (), desired.height));
		}
	}

	double row_stars = 0.0;
	for (int i = 0; i < row_count; i ++) {
		RowDefinition *rowdef = rows->GetValueAt (i)->AsRowDefinition ();
		GridLength* height = rowdef->GetHeight();

		switch (height->type) {
		case GridUnitTypeStar:
			row_stars += height->val;
			break;
		case GridUnitTypePixel:
			rowdef->SetActualHeight (height->val);
			remaining.height -= height->val;
			break;
		case GridUnitTypeAuto:
			remaining.height -= rowdef->GetActualHeight ();
			break;
		}
	}

	double col_stars = 0.0;
	for (int i = 0; i < col_count; i ++) {
		ColumnDefinition *coldef = columns->GetValueAt (i)->AsColumnDefinition ();
		GridLength* width = coldef->GetWidth();

		switch (width->type) {
		case GridUnitTypeStar:
			col_stars += width->val;
			break;
		case GridUnitTypePixel:
			coldef->SetActualWidth (width->val);
			remaining.width -= width->val;
			break;
		case GridUnitTypeAuto:
			remaining.width -= coldef->GetActualWidth ();
			break;
		}
	}

	for (int i = 0; i < row_count; i ++) {
		RowDefinition *rowdef = rows->GetValueAt (i)->AsRowDefinition ();
		GridLength* height = rowdef->GetHeight();

		if (height->type == GridUnitTypeStar)
			rowdef->SetActualHeight (remaining.height * height->val / row_stars);
	}

	for (int i = 0; i < col_count; i ++) {
		ColumnDefinition *coldef = columns->GetValueAt (i)->AsColumnDefinition ();
		GridLength* width = coldef->GetWidth();

		if (width->type == GridUnitTypeStar)
			coldef->SetActualWidth (remaining.width * width->val / col_stars);
	}

	walker = VisualTreeWalker (this);
	while (UIElement *child = walker.Step ()) {
		gint32 col = Grid::GetColumn (child);
		gint32 row = Grid::GetRow (child);
		gint32 colspan = Grid::GetColumnSpan (child);
		gint32 rowspan = Grid::GetRowSpan (child);

		Rect child_final = Rect (0, 0, 0, 0);
		Size min_size;
		Size max_size;

		for (int r = 0; r < MIN (row + rowspan, row_count); r++) {
			RowDefinition *rowdef = rows->GetValueAt (r)->AsRowDefinition ();

			if (r < row) {
				child_final.y += rowdef->GetActualHeight ();
			} else {
				child_final.height += rowdef->GetActualHeight ();
				
				min_size.height += rowdef->GetMinHeight ();
				max_size.height += rowdef->GetMaxHeight ();
			}
		}



		for (int c = col; c < MIN (col + colspan, col_count); c++) {
			ColumnDefinition *coldef = columns->GetValueAt (c)->AsColumnDefinition ();

			if (c < col) {
				child_final.y += coldef->GetActualWidth ();
			} else {
				child_final.width += coldef->GetActualWidth ();

				min_size.width += coldef->GetMinWidth ();
				max_size.width += coldef->GetMaxWidth ();
			}
		}

		child->Arrange (child_final);
		
		/*
		  Size arranged = child.GetRenderSize ();
		arranged = arranged.Max (min_size);
		arranged = arranged.Min (max_size);
		*/
	}

	return finalSize;
}
