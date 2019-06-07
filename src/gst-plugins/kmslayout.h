#ifndef _KMS_LAYOUT_H_
#define _KMS_LAYOUT_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define KMS_TYPE_LAYOUT (kms_layout_get_type())

#define KMS_LAYOUT(obj) (       \
  G_TYPE_CHECK_INSTANCE_CAST(   \
    (obj),                      \
    KMS_TYPE_LAYOUT,            \
    KmsLayout                   \
  )                             \
)

#define KMS_LAYOUT_CLASS(klass) (   \
  G_TYPE_CHECK_CLASS_CAST (         \
    (klass),                        \
    KMS_TYPE_LAYOUT,                \
    KmsLayoutClass                  \
  )                                 \
)

#define KMS_IS_LAYOUT(obj) (        \
  G_TYPE_CHECK_INSTANCE_TYPE (      \
    (obj),                          \
    KMS_TYPE_LAYOUT                 \
  )                                 \
)

#define KMS_IS_LAYOUT_CLASS(klass) (  \
  G_TYPE_CHECK_CLASS_TYPE (           \
    (klass),                          \
    KMS_TYPE_LAYOUT                   \
  )                                   \
)

#define KMS_LAYOUT_GET_CLASS(obj) (   \
  G_TYPE_INSTANCE_GET_CLASS (         \
    (obj),                            \
    KMS_TYPE_LAYOUT,                  \
    KmsLayoutClass                    \
  )                                   \
)

typedef struct _KmsLayout KmsLayout;
typedef struct _KmsLayoutClass KmsLayoutClass;
typedef struct _KmsLayoutPrivate KmsLayoutPrivate;
typedef struct _KmsUser KmsUser;
typedef struct _KmsPosition KmsPosition;

struct _KmsLayout {
  GObject parent;

  /* Private */
  KmsLayoutPrivate *priv;
};

struct _KmsLayoutClass {
  GObjectClass parent_class;

};

enum {
  LAYOUT_0,
  LAYOUT_1,
  LAYOUT_2
};

GType kms_layout_get_type ();

KmsLayout * kms_layout_new();

void kms_layout_create_new_user(KmsLayout * self, GstElement * capsfilter,
  GstPad * video_mixer_pad, gint id);

void kms_layout_set_floor(KmsLayout * self, gint id);

void kms_layout_remove_user(KmsLayout * self, gint id);

gint kms_layout_get_layout_type(KmsLayout * self);

void kms_layout_set_layout_width(KmsLayout * self, gint width);

void kms_layout_set_layout_height(KmsLayout * self, gint height);

void kms_layout_change_layout(KmsLayout * self, gint id);

void kms_layout_try_insert(KmsLayout * self, gint id);

gboolean kms_layout_already_registered(KmsLayout * self, gint id);

G_END_DECLS

#endif
