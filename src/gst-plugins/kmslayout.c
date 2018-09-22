#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "kmslayout.h"
#include <stdio.h>
#include <gobject/gvaluecollector.h>
#include <gst/gst.h>
#include <glib-object.h>
#include <math.h>

#define OBJECT_NAME "layout"

#define INVALID_ID -1
#define INFINITE -1
#define FLOOR_INDEX 0

#define KMS_LAYOUT_LOCK(layout) \
  (g_rec_mutex_lock (&(layout)->priv->mutex))

#define KMS_LAYOUT_UNLOCK(layout) \
  (g_rec_mutex_unlock (&(layout)->priv->mutex))

#define parent_class kms_layout_parent_class

#define KMS_LAYOUT_GET_PRIVATE(obj) (         \
  G_TYPE_INSTANCE_GET_PRIVATE (               \
    (obj),                                    \
    KMS_TYPE_LAYOUT,                         \
    KmsLayoutPrivate                          \
  )                                           \
)

struct _KmsUser
{
    GstElement *capsfilter;
    GstPad *video_mixer_pad;
    gint id;
};

struct _KmsPosition
{
    gint width;
    gint height;
    gint top;
    gint left;
    gboolean available;
    KmsUser * user;
};

struct _KmsLayoutPrivate
{
  GArray *position_array;
  GArray *user_array;
  GRecMutex mutex;
  gint max_users;
  gint type;
  gint width;
  gint height;
};

/* class initialization */

G_DEFINE_TYPE(KmsLayout, kms_layout, G_TYPE_OBJECT);

void kms_layout_position_init(KmsPosition * position, gint width, gint height,
  gint top, gint left)
{
  position->width = width;
  position->height = height;
  position->top = top;
  position->left = left;
  position->available = TRUE;
}
/**
 * Update a position's video on the composition
 * 
 * @param position 
 * Needs to be called with LOCK
 */
void kms_layout_update_position_output(KmsPosition * position)
{
  GstCaps *filtercaps = NULL;
  KmsUser *user = position->user;

  filtercaps = gst_caps_new_simple ("video/x-raw",
    "width", G_TYPE_INT, position->width, "height", G_TYPE_INT, position->height,
    "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  g_object_set(user->capsfilter, "caps", filtercaps, NULL);
  g_object_set(user->video_mixer_pad, "xpos", position->left, "ypos", position->top,
      "alpha", 1.0, NULL);
  gst_caps_unref(filtercaps);
}

/**
 * Hide a user's on the composition
 * 
 * @param user 
 * Needs to be called with LOCK
 */
void kms_layout_hide_user(KmsUser * user)
{
  GstCaps *filtercaps = NULL;

  filtercaps = gst_caps_new_simple ("video/x-raw",
    "width", G_TYPE_INT, 0, "height", G_TYPE_INT, 0,
    "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  g_object_set(user->capsfilter, "caps", filtercaps, NULL);
  g_object_set(user->video_mixer_pad, "xpos", 0, "ypos", 0,
      "alpha", 0, NULL);
  gst_caps_unref(filtercaps);
}

/**
 * Release an GArray of KmsPosition *
 *
 * @param array GArray of KmsPosition *
 * Needs to be called with LOCK
 */
void kms_layout_release_position_array(GArray * array)
{
  gint len = array->len, i = 0;
  KmsPosition *position = NULL;

  for(i = 0; i < len; i++)
  {
    position = g_array_index(array, KmsPosition *, i);
    if (position)
      g_slice_free(KmsPosition, position);
  }
   if (array)
    g_array_unref(array);
}
/**
 * Release an GArray of KmsUser *
 *
 * @param array GArray of KmsUser *
 * Needs to be called with LOCK
 */
void kms_layout_release_user_array(GArray * array)
{
  gint len = array->len, i = 0;
  KmsUser *user = NULL;

  for(i = 0; i < len; i++)
  {
    user = g_array_index(array, KmsUser *, i);
    if (user)
      g_slice_free(KmsUser, user);
  }
   if (array)
    g_array_unref(array);
}

/**
 * Reset the given position
 *
 * @param position KmsPosition
 * Needs to be called with LOCK
 */
void kms_layout_reset_position(KmsPosition * position)
{
  if (position->user)
  {
    KmsUser *user = position->user;
    GstCaps *filtercaps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, 0, "height", G_TYPE_INT, 0,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
    g_object_set(user->capsfilter, "caps", filtercaps, NULL);
    g_object_set(user->video_mixer_pad, "xpos", 0, "ypos", 0,
        "alpha", 0, NULL);
    gst_caps_unref(filtercaps);
  }
  position->available = TRUE;
  position->user = NULL;
}

/**
 * Tranfers the user from old_positions to new_positions
 *
 * @param old_positions GArray of KmsPosition *
 * @param new_positions GArray of KmsPosition *
 * Needs to be called with LOCK
 */
void kms_layout_transfer_users(GArray * old_positions, GArray * new_positions)
{
  gint old_len, new_len, i;
  KmsPosition *old = NULL, *new = NULL;
  old_len = old_positions->len;
  new_len = new_positions->len;
  for(i = 0; i < old_len; i++)
  {
    if (i >= new_len)
      break;
    old = g_array_index(old_positions, KmsPosition *, i);
    new = g_array_index(new_positions, KmsPosition *, i);
    if (old->user)
    {
      new->user = old->user;
      kms_layout_update_position_output(new);
      new->available = FALSE;
    }
  }
  for(; i < old_len; i++)
  {
    old = g_array_index(old_positions, KmsPosition *, i);
    kms_layout_reset_position(old);
  }
}

/**
 * Find an available position on the layout
 * @param  self Layout
 * @return      KmsLayoutData
 * Need to be called with LOCK
 */
KmsPosition * kms_layout_find_available_position(KmsLayout *self)
{
  gint i = 0, len = 0;
  KmsPosition *position;
  len = self->priv->position_array->len;

  /* if infinite layout (kurento default) */
  if (self->priv->type == LAYOUT_0) {
    position = g_slice_new0(KmsPosition);
    g_array_append_val(self->priv->position_array, position);
    GST_ERROR("01");
    return position;
  }
  for (i = 0; i < len; i++)
  {
    position = g_array_index(self->priv->position_array, KmsPosition *, i);

    /* Available slot */
    if (position && position->available)
    {
      GST_ERROR("02");
      return position;
    }
  }
  GST_ERROR("03");
  return NULL;
}

/**
 * Find the true index of the array for the given id
 * @param  self Layout
 * @param  id   User Id
 * @return      Array Index
 * Needs to be called with LOCK
 */
gint kms_layout_find_position_index_array(GArray *array, gint id)
{
  if (array == NULL) return INVALID_ID;

  gint i = 0, len = array->len;
  KmsPosition *position;

  for (i = 0; i < len; i++)
  {
    position = g_array_index(array, KmsPosition *, i);
    if (position && position->user && position->user->id == id)
    {
      return i;
    }
  }
  return INVALID_ID;
}

/**
 * Find the index of the user array for a given id
 *
 * @param array user_array
 * @param id int
 * @return gint index
 * Needs to be called with LOCK
 */
gint kms_layout_find_user_index_array(GArray *array, gint id)
{
  if (array == NULL) return INVALID_ID;

  gint i = 0, len = array->len;
  KmsUser *user;

  for (i = 0; i < len; i++)
  {
    user = g_array_index(array, KmsUser *, i);
    if (user->id == id)
    {
      return i;
    }
  }
  return INVALID_ID;
}

/**
 * @Retrive the position that has the user of the given id from the array
 *
 * @param array GArray of KmsPosition *
 * @param id gint
 * @return KmsPosition *
 * Needs to be called with LOCK
 */
KmsPosition * kms_layout_find_position_array(GArray *array, gint id)
{
  if (array == NULL) return NULL;

  gint i = 0, len = array->len;
  KmsPosition *position;

  for (i = 0; i < len; i++)
  {
    position = g_array_index(array, KmsPosition *, i);
    if (position && position->user && position->user->id == id)
    {
      return position;
    }
  }
  return NULL;
}

/**
 * Retrieve the user that has the given id from the array
 *
 * @param array GArray of KmsUser *
 * @param id
 * @return KmsUser *
 * Needs to be called with LOCK
 */
KmsUser * kms_layout_find_user_array(GArray *array, gint id)
{
  if (array == NULL) return NULL;

  gint i = 0, len = array->len;
  KmsUser *user;

  for (i = 0; i < len; i++)
  {
    user = g_array_index(array, KmsUser *, i);
    if (user && user->id == id)
    {
      return user;
    }
  }
  return NULL;
}

/**
 * Initializes a KmsUser structure
 *
 * @param user KmsUser
 * @param capsfilter GstElement
 * @param video_mixer_pad GstPad
 * @param id gint
 */
void kms_layout_init_user(KmsUser * user, GstElement * capsfilter,
  GstPad * video_mixer_pad, gint id)
{
  user->capsfilter = capsfilter;
  user->video_mixer_pad = video_mixer_pad;
  user->id = id;
}

/**
 * Recalculate the output of every user of this layout
 *
 * @param self KmsLayout
 * LOCK SAFE
 */
void kms_layout_recalculate_all_users(KmsLayout * self)
{
  gint width, height, top, left, n_columns, n_rows, n_elems;
  gint i;
  KmsPosition *position = NULL;
  KMS_LAYOUT_LOCK(self);
  if(self->priv->position_array == NULL || self->priv->position_array->len == 0)
  {
    KMS_LAYOUT_UNLOCK(self);
    return;
  }
  n_elems = self->priv->position_array->len;
  n_columns = (gint) ceil (sqrt (n_elems));
  n_rows = (gint) ceil ((float) n_elems / (float) n_columns);
  width = self->priv->width / n_columns;
  height = self->priv->height / n_rows;
  for(i = 0; i < n_elems; i++)
  {
    top = ((i / n_columns) * height);
    left = ((i % n_columns) * width);
    position = g_array_index(self->priv->position_array, KmsPosition *, i);
    position->width = width;
    position->height = height;
    position->top = top;
    position->left = left;
    if (position && position->user)
      kms_layout_update_position_output(position);
  }
  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Insert the given user into the given position
 *
 * @param position KmsPosition
 * @param user KmsUser
 * Needs to be called with LOCK
 */
void kms_layout_insert_user(KmsPosition * position, KmsUser * user)
{
  if (position == NULL || user == NULL) return;
  position->user = user;
  position->available = FALSE;
}

/**
 * @Remove every empty KmsPosition of the given array
 *
 * @param positions GArray of KmsPosition *
 * Needs to be called with LOCK
 */
void kms_layout_remove_empty_slots(GArray * positions)
{
  gint i = 0, len = positions->len;
  KmsPosition *position;

  /* Inverse for to avoid problems when removing stuff from the array positions */
  for (i = (len-1); i >= 0; i--)
  {
    position = g_array_index(positions, KmsPosition *, i);
    if (position && position->available)
    {
      g_array_remove_index(positions, i);
    }
  }
}
/**
 * Change the layout to the LAYOUT_0
 * @param self Layout
 * LOCK SAFE
 */
void kms_layout_set_layout_0(KmsLayout *self) {
  KMS_LAYOUT_LOCK(self);
  if (self->priv->position_array == NULL)
    self->priv->position_array = g_array_new(FALSE, FALSE, sizeof(KmsPosition *));

  self->priv->max_users = 99; // infinite
  self->priv->type = LAYOUT_0;
  self->priv->width = 1280;
  self->priv->height = 720;
  kms_layout_remove_empty_slots(self->priv->position_array);
  kms_layout_recalculate_all_users(self);
  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Change the layout to the LAYOUT_1
 * @param self Layout
 * LOCK SAFE
 */
void kms_layout_set_layout_1(KmsLayout *self) {
  GArray *position_array = g_array_new(FALSE, FALSE, sizeof(KmsPosition *));
  KMS_LAYOUT_LOCK(self);
  gint width_cell = self->priv->width/3;
  gint height_cell = self->priv->height/3;
  KMS_LAYOUT_UNLOCK(self);
  /* Layout definition */
  {
    KmsPosition *user_0 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_0, width_cell*2, height_cell*2, 0, 0);
    g_array_append_val(position_array, user_0);

    KmsPosition *user_1 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_1, width_cell, height_cell, user_0->height, 0);
    g_array_append_val(position_array, user_1);

    KmsPosition *user_2 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_2, width_cell, height_cell, user_0->height,
      user_1->width);
    g_array_append_val(position_array, user_2);

    KmsPosition *user_3 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_3, width_cell, height_cell, user_0->height,
      user_2->width + user_2->left);
    g_array_append_val(position_array, user_3);

    KmsPosition *user_4 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_4, width_cell, height_cell, 0,
      user_0->width);
    g_array_append_val(position_array, user_4);

    KmsPosition *user_5 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_5, width_cell, height_cell, user_4->height,
      user_0->width);
    g_array_append_val(position_array, user_5);
  }
  KMS_LAYOUT_LOCK(self);
  if (self->priv->position_array != NULL)
  {
    kms_layout_transfer_users(self->priv->position_array, position_array);
    kms_layout_release_position_array(self->priv->position_array);
  }

  self->priv->position_array = position_array;
  self->priv->max_users = 6;
  self->priv->type = LAYOUT_1;
  self->priv->width = 1280;
  self->priv->height = 720;

  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Change the layout to the LAYOUT_2
 * @param self Layout
 * LOCK SAFE
 */
void kms_layout_set_layout_2(KmsLayout *self) {
  GArray *position_array = g_array_new(FALSE, FALSE, sizeof(KmsPosition *));
  KMS_LAYOUT_LOCK(self);
  gint width_cell = self->priv->width;
  gint height_cell = self->priv->height;
  KMS_LAYOUT_UNLOCK(self);
  /* Layout definition */
  {
    KmsPosition *user_0 = g_slice_new0(KmsPosition);
    kms_layout_position_init(user_0, width_cell, height_cell, 0, 0);
    g_array_append_val(position_array, user_0);
  }
  KMS_LAYOUT_LOCK(self);
  if (self->priv->position_array != NULL)
  {
    kms_layout_transfer_users(self->priv->position_array, position_array);
    kms_layout_release_position_array(self->priv->position_array);
  }
  self->priv->position_array = position_array;
  self->priv->max_users = 1;
  self->priv->type = LAYOUT_2;
  self->priv->width = 1280;
  self->priv->height = 720;

  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Change the Floor of the given Layout
 *
 * @param self KmsLayout
 * @param id gint
 * LOCK SAFE
 */
void kms_layout_set_floor(KmsLayout * self, gint id)
{
  KmsPosition *position = NULL, *floor_position = NULL;
  KmsUser *temp = NULL;
  KMS_LAYOUT_LOCK(self);
  if (self->priv->position_array == NULL ||
    self->priv->position_array->len <= 0)
  {
    KMS_LAYOUT_UNLOCK(self);
    return;
  }
  floor_position = g_array_index(self->priv->position_array, KmsPosition *, 0);
  position = kms_layout_find_position_array(self->priv->position_array, id);

  if (position)
  {
    temp = floor_position->user;
    floor_position->user = position->user;
    position->user = temp;

    kms_layout_update_position_output(floor_position);
    kms_layout_update_position_output(position);
    KMS_LAYOUT_UNLOCK(self);
    return;
  }

  temp = kms_layout_find_user_array(self->priv->user_array, id);
  if (temp == NULL)
  {
    KMS_LAYOUT_UNLOCK(self);
    return;
  }
  kms_layout_reset_position(floor_position);
  floor_position->available = FALSE;
  floor_position->user = temp;
  kms_layout_update_position_output(floor_position);

  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Create a new user in the layout
 *
 * @param self KmsLayout
 * @param capsfilter GstElement
 * @param video_mixer_pad GstPad
 * @param id gint
 * LOCK SAFE
 */
void kms_layout_create_new_user(KmsLayout * self, GstElement * capsfilter,
  GstPad *video_mixer_pad, gint id)
{
  KmsPosition *position = NULL;
  KmsUser *user = g_slice_new0(KmsUser);
  kms_layout_init_user(user, capsfilter, video_mixer_pad, id);

  KMS_LAYOUT_LOCK(self);
  g_array_append_val(self->priv->user_array, user);

  position = kms_layout_find_available_position(self);
  if (position == NULL)
  {
    kms_layout_hide_user(user);
    KMS_LAYOUT_UNLOCK(self);
    GST_ERROR("FOI");
    return;
  }
  
  kms_layout_insert_user(position, user);
  if (self->priv->type == LAYOUT_0)
  {
    KMS_LAYOUT_UNLOCK(self);
    kms_layout_recalculate_all_users(self);
  }
  else
  {
    kms_layout_update_position_output(position);
    KMS_LAYOUT_UNLOCK(self);
  }
}

/**
 * Remove an user from the layout
 *
 * @param self KmsLayout
 * @param id Int
 * SAFE LOCK
 */
void kms_layout_remove_user(KmsLayout * self, gint id)
{
  gint pos_idx = 0, user_idx = 0;
  KmsUser *user = NULL;
  KmsPosition *position = NULL;

  KMS_LAYOUT_LOCK(self);
  pos_idx = kms_layout_find_position_index_array(self->priv->position_array, id);
  if (pos_idx != INVALID_ID)
  {
    position = g_array_index(self->priv->position_array, KmsPosition *, pos_idx);
    kms_layout_reset_position(position);
    if (self->priv->type == LAYOUT_0)
    {
      g_array_remove_index(self->priv->position_array, pos_idx);
      if (position)
        g_slice_free(KmsPosition, position);
    }
  }
  user_idx = kms_layout_find_user_index_array(self->priv->user_array, id);
  if (user_idx == INVALID_ID)
  {
    KMS_LAYOUT_UNLOCK(self);
    return;
  }
  user = g_array_index(self->priv->user_array, KmsUser *, user_idx);
  g_array_remove_index(self->priv->user_array, user_idx);

  if(user) g_slice_free(KmsUser, user);
  if (self->priv->type == LAYOUT_0)
  {
    KMS_LAYOUT_UNLOCK(self);
    kms_layout_recalculate_all_users(self);
    return;
  }
  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Verifies if the given id is already registered in the layout
 * @param  self Layout
 * @param  id   Id
 * @return      gboolean
 * LOCK SAFE
 */
gboolean kms_layout_already_registered(KmsLayout * self, gint id)
{
  KMS_LAYOUT_LOCK(self);
  gint array_id = kms_layout_find_user_index_array(self->priv->user_array, id);
  KMS_LAYOUT_UNLOCK(self);

  if (array_id == INVALID_ID)
  {
    return FALSE;
  }
  return TRUE;
}

/**
 * Change the layout
 *
 * @param self Layout
 * @param id Id of the new type of layout
 * LOCK SAFE
 */
void kms_layout_change_layout(KmsLayout * self, gint id)
{
  switch(id)
  {
    case 0:
      KMS_LAYOUT_LOCK(self);
      self->priv->type = LAYOUT_0;
      KMS_LAYOUT_UNLOCK(self);
      kms_layout_set_layout_0(self);
      break;
    case 1:
      KMS_LAYOUT_LOCK(self);
      self->priv->type = LAYOUT_1;
      KMS_LAYOUT_UNLOCK(self);
      kms_layout_set_layout_1(self);
      break;
    case 2:
      KMS_LAYOUT_LOCK(self);
      self->priv->type = LAYOUT_2;
      KMS_LAYOUT_UNLOCK(self);
      kms_layout_set_layout_2(self);
      break;
    default:
      GST_WARNING("There is no layout %d", id);
      break;
  }
}

/**
 * Try to insert a user from the user_array into position_array
 * Id from a user thats already in user_array
 * @param self KmsLayout
 * @param id gint
 * LOCK SAFE
 */
void kms_layout_try_insert(KmsLayout * self, gint id)
{
  gint tempId;
  KmsPosition * position;
  KmsUser * user;
  KMS_LAYOUT_LOCK(self);
  tempId = kms_layout_find_position_index_array(self->priv->position_array, id);
  if (tempId != INVALID_ID)
  {
    KMS_LAYOUT_UNLOCK(self);
    return;
  }
  position = kms_layout_find_available_position(self);

  if (position == NULL)
  {
    KMS_LAYOUT_UNLOCK(self);
    return;
  }
  user = kms_layout_find_user_array(self->priv->user_array, id);
  position->user = user;
  position->available = FALSE;
  if (self->priv->type == LAYOUT_0)
  {
    KMS_LAYOUT_UNLOCK(self);
    kms_layout_recalculate_all_users(self);
    return;
  }
  kms_layout_update_position_output(position);
  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Setter Width
 * @param self   Layout
 * @param height Width
 * LOCK SAFE
 */
void kms_layout_set_layout_width(KmsLayout *self, gint width)
{
  KMS_LAYOUT_LOCK(self);
  self->priv->width = width;
  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Setter Height
 * @param self   Layout
 * @param height Height
 * LOCK SAFE
 */
void kms_layout_set_layout_height(KmsLayout *self, gint height)
{
  KMS_LAYOUT_LOCK(self);
  self->priv->height = height;
  KMS_LAYOUT_UNLOCK(self);
}

/**
 * Getter Layout Type
 *
 * @param self KmsLayout
 * @return gint
 * LOCK SAFE
 */
gint kms_layout_get_layout_type(KmsLayout *self)
{
  return self->priv->type;
}

/**
 * Is called when the object is finalized
 * @param  object Object Itself
 */

static void
kms_layout_finalize (GObject * object)
{
  KmsLayout *self = KMS_LAYOUT (object);

  g_rec_mutex_clear (&self->priv->mutex);
  if(self->priv->user_array)
    kms_layout_release_user_array(self->priv->user_array);
  if(self->priv->position_array)
    kms_layout_release_position_array(self->priv->position_array);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GObject *
kms_layout_contructor (GType gtype, guint n_properties,
  GObjectConstructParam *properties)
{
  GObject *obj;

  obj = G_OBJECT_CLASS (kms_layout_parent_class)->constructor (gtype, n_properties, properties);

  /* update the object state depending on constructor properties */

  return obj;
}

static void
kms_layout_class_init (KmsLayoutClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (kms_layout_finalize);
  gobject_class->constructor = kms_layout_contructor;

  /* Registers a private structure for the instantiatable type */

  g_type_class_add_private (klass, sizeof (KmsLayoutPrivate));
}

/**
 * Initialize the object
 */
static void
kms_layout_init (KmsLayout * self)
{
  self->priv = KMS_LAYOUT_GET_PRIVATE(self);

  g_rec_mutex_init (&self->priv->mutex);

  self->priv->type = LAYOUT_0;
  self->priv->max_users = 0;
  self->priv->user_array = g_array_new(FALSE, FALSE, sizeof(KmsUser *));
}

/**
 * Create a new instance of KmsLayout
 * @return KmsLayout
 */
KmsLayout *
kms_layout_new ()
{
  gpointer obj;

  obj = g_object_new(KMS_TYPE_LAYOUT, NULL);

  return KMS_LAYOUT (obj);
}
