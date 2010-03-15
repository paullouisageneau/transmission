/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "favicon.h" /* gtr_get_favicon() */
#include "filter.h"
#include "hig.h" /* GUI_PAD */
#include "tr-core.h" /* MC_TORRENT_RAW */
#include "util.h" /* gtr_idle_add() */

#define DIRTY_KEY          "tr-filter-dirty-key"
#define SESSION_KEY        "tr-session-key"
#define TEXT_KEY           "tr-filter-text-key"
#define TEXT_MODE_KEY      "tr-filter-text-mode-key"
#define TORRENT_MODEL_KEY  "tr-filter-torrent-model-key"

#if !GTK_CHECK_VERSION( 2,16,0 )
 /* FIXME: when 2.16 has been out long enough, it would be really nice to
  * get rid of this libsexy usage because of its makefile strangeness */
 #define USE_SEXY
 #include "sexy-icon-entry.h"
#endif

/***
****
****  CATEGORIES
****
***/

enum
{
    CAT_FILTER_TYPE_ALL,
    CAT_FILTER_TYPE_PRIVATE,
    CAT_FILTER_TYPE_PUBLIC,
    CAT_FILTER_TYPE_HOST,
    CAT_FILTER_TYPE_PARENT,
    CAT_FILTER_TYPE_PRI_HIGH,
    CAT_FILTER_TYPE_PRI_NORMAL,
    CAT_FILTER_TYPE_PRI_LOW,
    CAT_FILTER_TYPE_TAG,
    CAT_FILTER_TYPE_SEPARATOR,
};

enum
{
    CAT_FILTER_COL_NAME, /* human-readable name; ie, Legaltorrents */
    CAT_FILTER_COL_COUNT, /* how many matches there are */
    CAT_FILTER_COL_TYPE,
    CAT_FILTER_COL_HOST, /* pattern-matching text; ie, legaltorrents.com */
    CAT_FILTER_COL_PIXBUF,
    CAT_FILTER_N_COLS
};

static int
pstrcmp( const void * a, const void * b )
{
    return strcmp( *(const char**)a, *(const char**)b );
}

/* pattern-matching text; ie, legaltorrents.com */
static char*
get_host_from_url( const char * url )
{
    char * h = NULL;
    char * name;
    const char * first_dot;
    const char * last_dot;

    tr_urlParse( url, -1, NULL, &h, NULL, NULL );
    first_dot = strchr( h, '.' );
    last_dot = strrchr( h, '.' );

    if( ( first_dot ) && ( last_dot ) && ( first_dot != last_dot ) )
        name = g_strdup( first_dot + 1 );
    else
        name = g_strdup( h );

    tr_free( h );
    return name;
}

/* human-readable name; ie, Legaltorrents */
static char*
get_name_from_host( const char * host )
{
    char * name;
    const char * dot = strrchr( host, '.' );

    if( dot == NULL )
        name = g_strdup( host );
    else
        name = g_strndup( host, dot - host );

    *name = g_ascii_toupper( *name );

    return name;
}

static void
category_model_update_count( GtkTreeStore * store, GtkTreeIter * iter, int n )
{
    int count;
    GtkTreeModel * model = GTK_TREE_MODEL( store );
    gtk_tree_model_get( model, iter, CAT_FILTER_COL_COUNT, &count, -1 );
    if( n != count )
        gtk_tree_store_set( store, iter, CAT_FILTER_COL_COUNT, n, -1 );
}

static void
favicon_ready_cb( gpointer pixbuf, gpointer vreference )
{
    GtkTreeIter iter;
    GtkTreeRowReference * reference = vreference;

    if( pixbuf != NULL )
    {
        GtkTreePath * path = gtk_tree_row_reference_get_path( reference );
        GtkTreeModel * model = gtk_tree_row_reference_get_model( reference );

        if( gtk_tree_model_get_iter( model, &iter, path ) )
            gtk_tree_store_set( GTK_TREE_STORE( model ), &iter,
                                CAT_FILTER_COL_PIXBUF, pixbuf,
                                -1 );

        gtk_tree_path_free( path );

        g_object_unref( pixbuf );
    }

    gtk_tree_row_reference_free( reference );
}

static gboolean
category_filter_model_update( GtkTreeStore * store )
{
    int i, n;
    int low = 0;
    int all = 0;
    int high = 0;
    int public = 0;
    int normal = 0;
    int private = 0;
    int store_pos;
    GtkTreeIter iter;
    GtkTreeIter parent;
    GtkTreeModel * model = GTK_TREE_MODEL( store );
    GPtrArray * hosts = g_ptr_array_new( );
    GHashTable * hosts_hash = g_hash_table_new_full( g_str_hash, g_str_equal,
                                                     g_free, g_free );
    GObject * o = G_OBJECT( store );
    GtkTreeModel * tmodel = GTK_TREE_MODEL(
                                    g_object_get_data( o, TORRENT_MODEL_KEY ) );

    g_object_steal_data( o, DIRTY_KEY );

    /* walk through all the torrents, tallying how many matches there are
     * for the various categories.  also make a sorted list of all tracker
     * hosts s.t. we can merge it with the existing list */
    if( gtk_tree_model_get_iter_first( tmodel, &iter )) do
    {
        tr_torrent * tor;
        const tr_info * inf;
        int keyCount;
        char ** keys;

        gtk_tree_model_get( tmodel, &iter, MC_TORRENT_RAW, &tor, -1 );
        inf = tr_torrentInfo( tor );
        keyCount = 0;
        keys = g_new( char*, inf->trackerCount );

        for( i=0, n=inf->trackerCount; i<n; ++i )
        {
            int k;
            char * key = get_host_from_url( inf->trackers[i].announce );
            int * count = g_hash_table_lookup( hosts_hash, key );
            if( count == NULL )
            {
                char * k = g_strdup( key );
                count = tr_new0( int, 1 );
                g_hash_table_insert( hosts_hash, k, count );
                g_ptr_array_add( hosts, k );
            }

            for( k=0; k<keyCount; ++k )
                if( !strcmp( keys[k], key ) )
                    break;
            if( k==keyCount )
                keys[keyCount++] = key;
            else
                g_free( key );
        }

        for( i=0; i<keyCount; ++i )
        {
            int * incrementme = g_hash_table_lookup( hosts_hash, keys[i] );
            ++*incrementme;
            g_free( keys[i] );
        }
        g_free( keys );

        ++all;

        if( inf->isPrivate )
            ++private;
        else
            ++public;

        switch( tr_torrentGetPriority( tor ) )
        {
            case TR_PRI_HIGH: ++high; break;
            case TR_PRI_LOW: ++low; break;
            default: ++normal; break;
        }
    }
    while( gtk_tree_model_iter_next( tmodel, &iter ) );
    qsort( hosts->pdata, hosts->len, sizeof(char*), pstrcmp );

    /* update the "all" count */
    gtk_tree_model_iter_nth_child( model, &iter, NULL, 0 );
    category_model_update_count( store, &iter, all );

    /* update the "public" subtree */
    gtk_tree_model_iter_nth_child( model, &parent, NULL, 2 );
    gtk_tree_model_iter_nth_child( model, &iter, &parent, 0 );
    category_model_update_count( store, &iter, public );
    gtk_tree_model_iter_nth_child( model, &iter, &parent, 1 );
    category_model_update_count( store, &iter, private );

    /* update the "priority" subtree */
    gtk_tree_model_iter_nth_child( model, &parent, NULL, 3 );
    gtk_tree_model_iter_nth_child( model, &iter, &parent, 0 );
    category_model_update_count( store, &iter, high );
    gtk_tree_model_iter_nth_child( model, &iter, &parent, 1 );
    category_model_update_count( store, &iter, normal );
    gtk_tree_model_iter_nth_child( model, &iter, &parent, 2 );
    category_model_update_count( store, &iter, low );

    /* update the "hosts" subtree */
    gtk_tree_model_iter_nth_child( model, &parent, NULL, 4 );
    for( i=store_pos=0, n=hosts->len ; ; )
    {
        const gboolean new_hosts_done = i >= n;
        const gboolean old_hosts_done = !gtk_tree_model_iter_nth_child( model, &iter, &parent, store_pos );
        gboolean remove_row = FALSE;
        gboolean insert_row = FALSE;

        /* are we done yet? */
        if( new_hosts_done && old_hosts_done )
            break;

        /* decide what to do */
        if( new_hosts_done ) {
            /* g_message( "new hosts done; remove row" ); */
            remove_row = TRUE;
        } else if( old_hosts_done ) {
            /* g_message( "old hosts done; insert row" ); */
            insert_row = TRUE;
        } else {
            int cmp;
            char * host;
            gtk_tree_model_get( model, &iter, CAT_FILTER_COL_HOST, &host,  -1 );
            cmp = strcmp( host, hosts->pdata[i] );
            /* g_message( "cmp( %s, %s ) returns %d", host, (char*)hosts->pdata[i], cmp ); */
            if( cmp < 0 ) {
                /* g_message( "cmp<0, so remove row" ); */
                remove_row = TRUE;
            } else if( cmp > 0 ) {
                /* g_message( "cmp>0, so insert row" ); */
                insert_row = TRUE;
            }
            g_free( host );
        }

        /* do something */
        if( remove_row ) {
            /* g_message( "removing row and incrementing i" ); */
            gtk_tree_store_remove( store, &iter );
        } else if( insert_row ) {
            GtkTreeIter child;
            GtkTreePath * path;
            GtkTreeRowReference * reference;
            tr_session * session = g_object_get_data( G_OBJECT( store ), SESSION_KEY );
            const char * host = hosts->pdata[i];
            char * name = get_name_from_host( host );
            const int count = *(int*)g_hash_table_lookup( hosts_hash, host );
            gtk_tree_store_insert_with_values( store, &child, &parent, store_pos,
                CAT_FILTER_COL_HOST, host,
                CAT_FILTER_COL_NAME, name,
                CAT_FILTER_COL_COUNT, count,
                CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_HOST,
                -1 );
            path = gtk_tree_model_get_path( model, &child );
            reference = gtk_tree_row_reference_new( model, path );
            gtr_get_favicon( session, host, favicon_ready_cb, reference );
            gtk_tree_path_free( path );
            g_free( name );
            ++store_pos;
            ++i;
        } else { /* update row */
            const char * host = hosts->pdata[i];
            const int count = *(int*)g_hash_table_lookup( hosts_hash, host );
            category_model_update_count( store, &iter, count );
            ++store_pos;
            ++i;
        }
    }
    
    /* cleanup */
    g_ptr_array_free( hosts, TRUE );
    g_hash_table_unref( hosts_hash );
    g_ptr_array_foreach( hosts, (GFunc)g_free, NULL );
    return FALSE;
}

static GtkTreeModel *
category_filter_model_new( GtkTreeModel * tmodel )
{
    GtkTreeIter iter;
    GtkTreeStore * store;
    const int invisible_number = -1; /* doesn't get rendered */

    store = gtk_tree_store_new( CAT_FILTER_N_COLS,
                                G_TYPE_STRING,
                                G_TYPE_INT,
                                G_TYPE_INT,
                                G_TYPE_STRING,
                                GDK_TYPE_PIXBUF );

    gtk_tree_store_insert_with_values( store, NULL, NULL, -1,
        CAT_FILTER_COL_NAME, _( "All" ),
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_ALL,
        -1 );
    gtk_tree_store_insert_with_values( store, NULL, NULL, -1,
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_SEPARATOR,
        -1 );

    gtk_tree_store_insert_with_values( store, &iter, NULL, -1,
        CAT_FILTER_COL_NAME, _( "Privacy" ),
        CAT_FILTER_COL_COUNT, invisible_number,
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PARENT,
        -1 );
    gtk_tree_store_insert_with_values( store, NULL, &iter, -1,
        CAT_FILTER_COL_NAME, _( "Public" ),
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PUBLIC,
        -1 );
    gtk_tree_store_insert_with_values( store, NULL, &iter, -1,
        CAT_FILTER_COL_NAME, _( "Private" ),
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PRIVATE,
        -1 );

    gtk_tree_store_insert_with_values( store, &iter, NULL, -1,
        CAT_FILTER_COL_NAME, _( "Priority" ),
        CAT_FILTER_COL_COUNT, invisible_number,
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PARENT,
        -1 );
    gtk_tree_store_insert_with_values( store, NULL, &iter, -1,
        CAT_FILTER_COL_NAME, _( "High" ),
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PRI_HIGH,
        -1 );
    gtk_tree_store_insert_with_values( store, NULL, &iter, -1,
        CAT_FILTER_COL_NAME, _( "Normal" ),
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PRI_NORMAL,
        -1 );
    gtk_tree_store_insert_with_values( store, NULL, &iter, -1,
        CAT_FILTER_COL_NAME, _( "Low" ),
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PRI_LOW,
        -1 );

    gtk_tree_store_insert_with_values( store, &iter, NULL, -1,
        CAT_FILTER_COL_NAME, _( "Trackers" ),
        CAT_FILTER_COL_COUNT, invisible_number,
        CAT_FILTER_COL_TYPE, CAT_FILTER_TYPE_PARENT,
        -1 );

    g_object_set_data( G_OBJECT( store ), TORRENT_MODEL_KEY, tmodel );
    category_filter_model_update( store );
    return GTK_TREE_MODEL( store );
}

static gboolean
is_it_a_separator( GtkTreeModel * m, GtkTreeIter * iter, gpointer data UNUSED )
{
    int type;
    gtk_tree_model_get( m, iter, CAT_FILTER_COL_TYPE, &type, -1 );
    return type == CAT_FILTER_TYPE_SEPARATOR;
}

static void
category_model_update_idle( gpointer category_model )
{
    GObject * o = G_OBJECT( category_model );
    const gboolean pending = g_object_get_data( o, DIRTY_KEY ) != NULL;
    if( !pending )
    {
        GSourceFunc func = (GSourceFunc) category_filter_model_update;
        g_object_set_data( o, DIRTY_KEY, GINT_TO_POINTER(1) );
        gtr_idle_add( func, category_model );
    }
}

static void
torrent_model_row_changed( GtkTreeModel  * tmodel UNUSED,
                           GtkTreePath   * path UNUSED,
                           GtkTreeIter   * iter UNUSED,
                           gpointer        category_model )
{
    category_model_update_idle( category_model );
}

static void
torrent_model_row_deleted_cb( GtkTreeModel * tmodel UNUSED,
                              GtkTreePath  * path UNUSED,
                              gpointer       category_model )
{
    category_model_update_idle( category_model );
}

static void
render_pixbuf_func( GtkCellLayout    * cell_layout UNUSED,
                    GtkCellRenderer  * cell_renderer,
                    GtkTreeModel     * tree_model,
                    GtkTreeIter      * iter,
                    gpointer           data UNUSED )
{
    int type;
    gtk_tree_model_get( tree_model, iter, CAT_FILTER_COL_TYPE, &type, -1 );
    g_object_set( cell_renderer, "width", type==CAT_FILTER_TYPE_HOST ? 20 : 0, NULL );
}
static void
render_number_func( GtkCellLayout    * cell_layout UNUSED,
                    GtkCellRenderer  * cell_renderer,
                    GtkTreeModel     * tree_model,
                    GtkTreeIter      * iter,
                    gpointer           data UNUSED )
{
    int count;
    char buf[512];

    gtk_tree_model_get( tree_model, iter, CAT_FILTER_COL_COUNT, &count, -1 );

    if( count >= 0 )
        g_snprintf( buf, sizeof( buf ), "%'d", count );
    else
        *buf = '\0';

    g_object_set( cell_renderer, "text", buf, NULL );
}

static GtkCellRenderer *
number_renderer_new( void )
{
    GtkCellRenderer * r = gtk_cell_renderer_text_new( );

    g_object_set( G_OBJECT( r ), "alignment", PANGO_ALIGN_RIGHT,
                                 "weight", PANGO_WEIGHT_ULTRALIGHT,
                                 "xalign", 1.0,
                                 "xpad", GUI_PAD,
                                 NULL );

    return r;
}

static GtkWidget *
category_combo_box_new( GtkTreeModel * tmodel )
{
    GtkWidget * c;
    GtkCellRenderer * r;
    GtkTreeModel * category_model;

    /* create the category combobox */
    category_model = category_filter_model_new( tmodel );
    c = gtk_combo_box_new_with_model( category_model );
    gtk_combo_box_set_row_separator_func( GTK_COMBO_BOX( c ),
                                          is_it_a_separator, NULL, NULL );
    gtk_combo_box_set_active( GTK_COMBO_BOX( c ), 0 );

    r = gtk_cell_renderer_pixbuf_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( c ), r, FALSE );
    gtk_cell_layout_set_cell_data_func( GTK_CELL_LAYOUT( c ), r, render_pixbuf_func, NULL, NULL );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( c ), r,
                                    "pixbuf", CAT_FILTER_COL_PIXBUF,
                                    NULL );
    

    r = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( c ), r, FALSE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( c ), r,
                                    "text", CAT_FILTER_COL_NAME,
                                    NULL );

    r = number_renderer_new( );
    gtk_cell_layout_pack_end( GTK_CELL_LAYOUT( c ), r, TRUE );
    gtk_cell_layout_set_cell_data_func( GTK_CELL_LAYOUT( c ), r, render_number_func, NULL, NULL );

    g_signal_connect( tmodel, "row-changed",
                      G_CALLBACK( torrent_model_row_changed ), category_model );
    g_signal_connect( tmodel, "row-inserted",
                      G_CALLBACK( torrent_model_row_changed ), category_model );
    g_signal_connect( tmodel, "row-deleted",
                      G_CALLBACK( torrent_model_row_deleted_cb ), category_model );

    return c;
}

static gboolean
testCategory( GtkWidget * category_combo, tr_torrent * tor )
{
    int type;
    const tr_info * inf;
    GtkTreeIter iter;
    GtkComboBox * combo = GTK_COMBO_BOX( category_combo );
    GtkTreeModel * model = gtk_combo_box_get_model( combo );

    if( !gtk_combo_box_get_active_iter( combo, &iter ) )
        return TRUE;

    inf = tr_torrentInfo( tor );
    gtk_tree_model_get( model, &iter, CAT_FILTER_COL_TYPE, &type, -1 );
    switch( type )
    {
        case CAT_FILTER_TYPE_ALL:
            return TRUE;

        case CAT_FILTER_TYPE_PRIVATE:
            return inf->isPrivate;

        case CAT_FILTER_TYPE_PUBLIC:
            return !inf->isPrivate;

        case CAT_FILTER_TYPE_PRI_HIGH:
            return tr_torrentGetPriority( tor ) == TR_PRI_HIGH;

        case CAT_FILTER_TYPE_PRI_NORMAL:
            return tr_torrentGetPriority( tor ) == TR_PRI_NORMAL;

        case CAT_FILTER_TYPE_PRI_LOW:
            return tr_torrentGetPriority( tor ) == TR_PRI_LOW;

        case CAT_FILTER_TYPE_HOST: {
            int i;
            char * host;
            gtk_tree_model_get( model, &iter, CAT_FILTER_COL_HOST, &host, -1 );
            for( i=0; i<inf->trackerCount; ++i ) {
                char * tmp = get_host_from_url( inf->trackers[i].announce );
                const gboolean hit = !strcmp( tmp, host );
                g_free( tmp );
                if( hit )
                    break;
            }
            g_free( host );
            return i < inf->trackerCount;
        }

        case CAT_FILTER_TYPE_TAG:
            /* FIXME */
            return TRUE;

        default:
            return TRUE;
    }
}

/***
****
****  STATES
****
***/

enum
{
    STATE_FILTER_ALL,
    STATE_FILTER_DOWNLOADING,
    STATE_FILTER_SEEDING,
    STATE_FILTER_ACTIVE,
    STATE_FILTER_PAUSED,
    STATE_FILTER_QUEUED,
    STATE_FILTER_VERIFYING,
    STATE_FILTER_ERROR,
    STATE_FILTER_SEPARATOR
};

enum
{
    STATE_FILTER_COL_NAME,
    STATE_FILTER_COL_COUNT,
    STATE_FILTER_COL_TYPE,
    STATE_FILTER_N_COLS
};

static gboolean
state_is_it_a_separator( GtkTreeModel * m, GtkTreeIter * i, gpointer d UNUSED )
{
    int type;
    gtk_tree_model_get( m, i, STATE_FILTER_COL_TYPE, &type, -1 );
    return type == STATE_FILTER_SEPARATOR;
}

static gboolean
test_torrent_state( tr_torrent * tor, int type )
{
    const tr_stat * st = tr_torrentStat( tor );

    switch( type )
    {
        case STATE_FILTER_DOWNLOADING:
            return st->activity == TR_STATUS_DOWNLOAD;

        case STATE_FILTER_SEEDING:
            return st->activity == TR_STATUS_SEED;

        case STATE_FILTER_ACTIVE:
            return ( st->peersSendingToUs > 0 )
                || ( st->peersGettingFromUs > 0 )
                || ( st->activity == TR_STATUS_CHECK );

        case STATE_FILTER_PAUSED:
            return st->activity == TR_STATUS_STOPPED;

        case STATE_FILTER_QUEUED:
            return FALSE;

        case STATE_FILTER_VERIFYING:
            return ( st->activity == TR_STATUS_CHECK_WAIT )
                || ( st->activity == TR_STATUS_CHECK );

        case STATE_FILTER_ERROR:
            return st->error != 0;

        default: /* STATE_FILTER_ALL */
            return TRUE;

    }
}

static gboolean
testState( GtkWidget * state_combo, tr_torrent * tor )
{
    int type;
    GtkTreeIter iter;
    GtkComboBox * combo = GTK_COMBO_BOX( state_combo );
    GtkTreeModel * model = gtk_combo_box_get_model( combo );

    if( !gtk_combo_box_get_active_iter( combo, &iter ) )
        return TRUE;

    gtk_tree_model_get( model, &iter, STATE_FILTER_COL_TYPE, &type, -1 );
    return test_torrent_state( tor, type );
}

static void
status_model_update_count( GtkListStore * store, GtkTreeIter * iter, int n )
{
    int count;
    GtkTreeModel * model = GTK_TREE_MODEL( store );
    gtk_tree_model_get( model, iter, STATE_FILTER_COL_COUNT, &count, -1 );
    if( n != count )
        gtk_list_store_set( store, iter, STATE_FILTER_COL_COUNT, n, -1 );
}

static void
state_filter_model_update( GtkListStore * store )
{
    GtkTreeIter iter;
    GtkTreeModel * model = GTK_TREE_MODEL( store );
    GObject * o = G_OBJECT( store );
    GtkTreeModel * tmodel = GTK_TREE_MODEL( g_object_get_data( o, TORRENT_MODEL_KEY ) );

    g_object_steal_data( o, DIRTY_KEY );

    if( gtk_tree_model_get_iter_first( model, &iter )) do
    {
        int hits;
        int type;
        GtkTreeIter torrent_iter;

        gtk_tree_model_get( model, &iter, STATE_FILTER_COL_TYPE, &type, -1 );

        hits = 0;
        if( gtk_tree_model_get_iter_first( tmodel, &torrent_iter )) do {
            tr_torrent * tor;
            gtk_tree_model_get( tmodel, &torrent_iter, MC_TORRENT_RAW, &tor, -1 );
            if( test_torrent_state( tor, type ) )
                ++hits;
        } while( gtk_tree_model_iter_next( tmodel, &torrent_iter ) );

        status_model_update_count( store, &iter, hits );

    } while( gtk_tree_model_iter_next( model, &iter ) );
}

static GtkTreeModel *
state_filter_model_new( GtkTreeModel * tmodel )
{
    int i, n;
    struct {
        int type;
        const char * name;
    } types[] = {
        { STATE_FILTER_ALL, N_( "All" ) },
        { STATE_FILTER_SEPARATOR, NULL },
        { STATE_FILTER_DOWNLOADING, N_( "Downloading" ) },
        { STATE_FILTER_SEEDING, N_( "Seeding" ) },
        { STATE_FILTER_ACTIVE, N_( "Active" ) },
        { STATE_FILTER_PAUSED, N_( "Paused" ) },
        { STATE_FILTER_QUEUED, N_( "Queued" ) },
        { STATE_FILTER_VERIFYING, N_( "Verifying" ) },
        { STATE_FILTER_ERROR, N_( "Error" ) }
    };
    GtkListStore * store;

    store = gtk_list_store_new( STATE_FILTER_N_COLS,
                                G_TYPE_STRING,
                                G_TYPE_INT,
                                G_TYPE_INT );
    for( i=0, n=G_N_ELEMENTS(types); i<n; ++i )
        gtk_list_store_insert_with_values( store, NULL, -1,
                                           STATE_FILTER_COL_NAME, _( types[i].name ),
                                           STATE_FILTER_COL_TYPE, types[i].type,
                                           -1 );

    g_object_set_data( G_OBJECT( store ), TORRENT_MODEL_KEY, tmodel );
    state_filter_model_update( store );
    return GTK_TREE_MODEL( store );
}

static void
state_model_update_idle( gpointer state_model )
{
    GObject * o = G_OBJECT( state_model );
    const gboolean pending = g_object_get_data( o, DIRTY_KEY ) != NULL;
    if( !pending )
    {
        GSourceFunc func = (GSourceFunc) state_filter_model_update;
        g_object_set_data( o, DIRTY_KEY, GINT_TO_POINTER(1) );
        gtr_idle_add( func, state_model );
    }
}

static void
state_torrent_model_row_changed( GtkTreeModel  * tmodel UNUSED,
                                 GtkTreePath   * path UNUSED,
                                 GtkTreeIter   * iter UNUSED,
                                 gpointer        state_model )
{
    state_model_update_idle( state_model );
}

static void
state_torrent_model_row_deleted_cb( GtkTreeModel  * tmodel UNUSED,
                                    GtkTreePath   * path UNUSED,
                                    gpointer        state_model )
{
    state_model_update_idle( state_model );
}

static GtkWidget *
state_combo_box_new( GtkTreeModel * tmodel )
{
    GtkWidget * c;
    GtkCellRenderer * r;
    GtkTreeModel * state_model;

    state_model = state_filter_model_new( tmodel );
    c = gtk_combo_box_new_with_model( state_model );
    gtk_combo_box_set_row_separator_func( GTK_COMBO_BOX( c ),
                                          state_is_it_a_separator, NULL, NULL );
    gtk_combo_box_set_active( GTK_COMBO_BOX( c ), 0 );

    r = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( c ), r, TRUE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( c ), r, "text", STATE_FILTER_COL_NAME, NULL );

    r = number_renderer_new( );
    gtk_cell_layout_pack_end( GTK_CELL_LAYOUT( c ), r, TRUE );
    gtk_cell_layout_set_cell_data_func( GTK_CELL_LAYOUT( c ), r, render_number_func, NULL, NULL );

    g_signal_connect( tmodel, "row-changed",
                      G_CALLBACK( state_torrent_model_row_changed ), state_model );
    g_signal_connect( tmodel, "row-inserted",
                      G_CALLBACK( state_torrent_model_row_changed ), state_model );
    g_signal_connect( tmodel, "row-deleted",
                      G_CALLBACK( state_torrent_model_row_deleted_cb ), state_model );

    return c;
}

/****
*****
*****  ENTRY FIELD
*****
****/

enum
{
    TEXT_MODE_NAME,
    TEXT_MODE_FILES,
    TEXT_MODE_TRACKER,
    TEXT_MODE_N_TYPES
};

static gboolean
testText( const tr_torrent * tor, const char * key, int mode )
{
    gboolean ret;
    tr_file_index_t i;
    const tr_info * inf = tr_torrentInfo( tor );

    switch( mode )
    {
        case TEXT_MODE_FILES:
            for( i=0; i<inf->fileCount && !ret; ++i ) {
                char * pch = g_utf8_casefold( inf->files[i].name, -1 );
                ret = !key || strstr( pch, key ) != NULL;
                g_free( pch );
            }
            break;

        case TEXT_MODE_TRACKER:
            if( inf->trackerCount > 0 ) {
                char * pch = g_utf8_casefold( inf->trackers[0].announce, -1 );
                ret = !key || ( strstr( pch, key ) != NULL );
                g_free( pch );
            }
            break;

        default: /* NAME */
            if( !inf->name )
                ret = TRUE;
            else {
                char * pch = g_utf8_casefold( inf->name, -1 );
                ret = !key || ( strstr( pch, key ) != NULL );
                g_free( pch );
            }
            break;
    }

    return ret;
}


#ifdef USE_SEXY
static void
entry_icon_released( SexyIconEntry           * entry  UNUSED,
                     SexyIconEntryPosition     icon_pos,
                     int                       button UNUSED,
                     gpointer                  menu )
{
    if( icon_pos == SEXY_ICON_ENTRY_PRIMARY )
        gtk_menu_popup( GTK_MENU( menu ), NULL, NULL, NULL, NULL, 0,
                        gtk_get_current_event_time( ) );
}
#else
static void
entry_icon_release( GtkEntry              * entry  UNUSED,
                    GtkEntryIconPosition    icon_pos,
                    GdkEventButton        * event  UNUSED,
                    gpointer                menu )
{
    if( icon_pos == GTK_ENTRY_ICON_SECONDARY )
        gtk_entry_set_text( entry, "" );

    if( icon_pos == GTK_ENTRY_ICON_PRIMARY )
        gtk_menu_popup( GTK_MENU( menu ), NULL, NULL, NULL, NULL, 0,
                        gtk_get_current_event_time( ) );
}
#endif

static void
filter_entry_changed( GtkEditable * e, gpointer filter_model )
{
    char * pch;
    char * folded;
    
    pch = gtk_editable_get_chars( e, 0, -1 );
    folded = g_utf8_casefold( pch, -1 );
    g_object_set_data_full( filter_model, TEXT_KEY, folded, g_free );
    g_free( pch );

    gtk_tree_model_filter_refilter( GTK_TREE_MODEL_FILTER( filter_model ) );
}

static void
filter_text_toggled_cb( GtkCheckMenuItem * menu_item, gpointer filter_model )
{
    g_object_set_data( filter_model, TEXT_MODE_KEY,
                       g_object_get_data( G_OBJECT( menu_item ), TEXT_MODE_KEY ) );
    gtk_tree_model_filter_refilter( GTK_TREE_MODEL_FILTER( filter_model ) );
}

/*****
******
******
******
*****/

struct filter_data
{
    GtkWidget * state;
    GtkWidget * category;
    GtkWidget * entry;
    GtkTreeModel * filter_model;
};

static gboolean
is_row_visible( GtkTreeModel * model, GtkTreeIter * iter, gpointer vdata )
{
    int mode;
    gboolean b;
    const char * text;
    tr_torrent * tor;
    struct filter_data * data = vdata;
    GObject * o = G_OBJECT( data->filter_model );

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );

    text = (const char*) g_object_get_data( o, TEXT_KEY );
    mode = GPOINTER_TO_INT( g_object_get_data( o, TEXT_MODE_KEY ) );

    b = ( tor != NULL ) && testCategory( data->category, tor )
                        && testState( data->state, tor )
                        && testText( tor, text, mode );

    return b;
}

static void
selection_changed_cb( GtkComboBox * combo UNUSED, gpointer vdata )
{
    struct filter_data * data = vdata;
    gtk_tree_model_filter_refilter( GTK_TREE_MODEL_FILTER( data->filter_model ) );
}

GtkWidget *
gtr_filter_bar_new( tr_session * session, GtkTreeModel * tmodel, GtkTreeModel ** filter_model )
{
    int i;
    GtkWidget * l;
    GtkWidget * w;
    GtkWidget * h;
    GtkWidget * s;
    GtkWidget * menu;
    GtkWidget * state;
    GtkWidget * category;
    GSList * sl;
    const char * str;
    struct filter_data * data;
    const char *  filter_text_names[] = {
        N_( "Name" ), N_( "Files" ), N_( "Tracker" )
    };


    data = g_new( struct filter_data, 1 );
    data->state = state = state_combo_box_new( tmodel );
    data->category = category = category_combo_box_new( tmodel );
    data->entry = NULL;
    data->filter_model = gtk_tree_model_filter_new( tmodel, NULL );

    g_object_set( G_OBJECT( data->category ), "width-request", 170, NULL );
    g_object_set_data( G_OBJECT( gtk_combo_box_get_model( GTK_COMBO_BOX( data->category ) ) ), SESSION_KEY, session );

    gtk_tree_model_filter_set_visible_func(
        GTK_TREE_MODEL_FILTER( data->filter_model ),
        is_row_visible, data, g_free );

    g_signal_connect( data->category, "changed", G_CALLBACK( selection_changed_cb ), data );
    g_signal_connect( data->state, "changed", G_CALLBACK( selection_changed_cb ), data );


    h = gtk_hbox_new( FALSE, GUI_PAD_SMALL );

    /* add the category combobox */
    str = _( "_Category:" );
    w = category;
    l = gtk_label_new( NULL );
    gtk_label_set_markup_with_mnemonic( GTK_LABEL( l ), str );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    gtk_box_pack_start( GTK_BOX( h ), l, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( h ), w, TRUE, TRUE, 0 );

    /* add a spacer */
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_widget_set_size_request( w, 0u, GUI_PAD_BIG );
    gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );

    /* add the state combobox */
    str = _( "_State:" );
    w = state;
    l = gtk_label_new( NULL );
    gtk_label_set_markup_with_mnemonic( GTK_LABEL( l ), str );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    gtk_box_pack_start( GTK_BOX( h ), l, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( h ), w, TRUE, TRUE, 0 );

    /* add a spacer */
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_widget_set_size_request( w, 0u, GUI_PAD_BIG );
    gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );

    /* add the entry field */
#ifdef USE_SEXY
    s = sexy_icon_entry_new( );
    sexy_icon_entry_add_clear_button( SEXY_ICON_ENTRY( s ) );
    w = gtk_image_new_from_stock( GTK_STOCK_FIND, GTK_ICON_SIZE_MENU );
    sexy_icon_entry_set_icon( SEXY_ICON_ENTRY( s ),
                              SEXY_ICON_ENTRY_PRIMARY,
                              GTK_IMAGE( w ) );
    g_object_unref( w );
    sexy_icon_entry_set_icon_highlight( SEXY_ICON_ENTRY( s ),
                                        SEXY_ICON_ENTRY_PRIMARY, TRUE );
#else
    s = gtk_entry_new( );
    gtk_entry_set_icon_from_stock( GTK_ENTRY( s ),
                                   GTK_ENTRY_ICON_PRIMARY,
                                   GTK_STOCK_FIND);
    gtk_entry_set_icon_from_stock( GTK_ENTRY( s ),
                                   GTK_ENTRY_ICON_SECONDARY,
                                   GTK_STOCK_CLEAR );
#endif

    menu = gtk_menu_new( );
    sl = NULL;
    for( i=0; i<TEXT_MODE_N_TYPES; ++i )
    {
        const char * name = _( filter_text_names[i] );
        GtkWidget *  w = gtk_radio_menu_item_new_with_label ( sl, name );
        sl = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM( w ) );
        g_object_set_data( G_OBJECT( w ), TEXT_MODE_KEY, GINT_TO_POINTER( i ) );
        g_signal_connect( w, "toggled", G_CALLBACK( filter_text_toggled_cb ), data->filter_model );  
        gtk_menu_shell_append( GTK_MENU_SHELL( menu ), w );
        gtk_widget_show( w );
    }
#ifdef USE_SEXY
    g_signal_connect( s, "icon-released", G_CALLBACK( entry_icon_released ), menu );
#else
    g_signal_connect( s, "icon-release", G_CALLBACK( entry_icon_release ), menu );
#endif

    gtk_box_pack_start( GTK_BOX( h ), s, TRUE, TRUE, 0 );
    g_signal_connect( s, "changed", G_CALLBACK( filter_entry_changed ), data->filter_model );

    *filter_model = data->filter_model;
    return h;
}