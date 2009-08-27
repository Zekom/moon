/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * tilesource.h
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007,2009 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#ifndef __TILESOURCE_H__
#define __TILESOURCE_H__

#include <stdio.h>
#include "dependencyobject.h"

/* @CBindingRequisite */
typedef bool (*get_image_uri_func) (int level, int posX, int posY, Uri *uri, void* user_data);
typedef void (*invalidate_tile_layer_func) (int level, int tilePositionX, int tilePositionY, int tileLayer, void *user_data);

/* @Version=2,Namespace=System.Windows.Media */
/* @CallInitialize */
class MultiScaleTileSource : public DependencyObject {
 protected:
	/* @PropertyType=double,Version=2.0,GenerateAccessors,GenerateManagedAccessors,ManagedAccess=Internal */
	const static int ImageWidthProperty;
	/* @PropertyType=double,Version=2.0,GenerateAccessors,GenerateManagedAccessors,ManagedAccess=Internal */
	const static int ImageHeightProperty;
	/* @PropertyType=gint32,Version=2.0,GenerateAccessors,GenerateManagedAccessors,ManagedAccess=Internal */
	const static int TileWidthProperty;
	/* @PropertyType=gint32,Version=2.0,GenerateAccessors,GenerateManagedAccessors,ManagedAccess=Internal */
	const static int TileHeightProperty;
	/* @PropertyType=gint32,Version=2.0,GenerateAccessors,GenerateManagedAccessors,ManagedAccess=Internal */
	const static int TileOverlapProperty;

	virtual ~MultiScaleTileSource () {}

	invalidate_tile_layer_func invalidate_cb;
	void *invalidate_data;

 public:
	get_image_uri_func get_tile_func;

	/* @GenerateCBinding,GeneratePInvoke,ManagedAccess=Internal */
	MultiScaleTileSource ();

	double GetImageWidth ();
	void SetImageWidth (double width);

	double GetImageHeight ();
	void SetImageHeight (double height);

	int GetTileWidth ();
	void SetTileWidth (int width);

	int GetTileHeight ();
	void SetTileHeight (int height);

	int GetTileOverlap ();
	void SetTileOverlap (int overlap);

	/* @GenerateCBinding,GeneratePInvoke */
	void set_image_uri_func (get_image_uri_func func);

	/* @GenerateCBinding,GeneratePInvoke,ManagedAccess=Internal */
	void InvalidateTileLayer (int level, int tilePositionX, int tilePositionY, int tileLayer);

	void set_invalidate_tile_layer_func (invalidate_tile_layer_func func, void *user_data);
};

#endif /* __TILESOURCE_H__ */
