#ifndef REGIONTABLEWIDGET_H
#define REGIONTABLEWIDGET_H
#include <QTableWidget>
#include <QItemDelegate>
#include <QComboBox>
#include <vector>
#include "Regions.h"
#include "TIPL/tipl.hpp"
struct ThreadData;
class tracking_window;


class ImageDelegate : public QItemDelegate
 {
     Q_OBJECT

 public:
    ImageDelegate(QObject *parent)
         : QItemDelegate(parent)
     {
     }

     QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const;
          void setEditorData(QWidget *editor, const QModelIndex &index) const;
          void setModelData(QWidget *editor, QAbstractItemModel *model,
                            const QModelIndex &index) const;
private slots:
    void emitCommitData();
 };

class RegionTableWidget : public QTableWidget
{
    Q_OBJECT
protected:
    void contextMenuEvent ( QContextMenuEvent * event );
private:
    tracking_window& cur_tracking_window;
    void do_action(QString action);
    void do_action(QString action,size_t roi_index);
    void whole_brain_points(std::vector<tipl::vector<3,short> >& points);
    bool load_multiple_roi_nii(QString file_name,bool is_mni_image);
private:
    template<typename func>
    void for_each_checked_region(func fun)
    {
        for (unsigned int roi_index = 0;roi_index < regions.size();++roi_index)
        {
            if (item(roi_index,0)->checkState() != Qt::Checked)
                continue;
            fun(regions[roi_index]);
        }
    }
    std::vector<std::shared_ptr<ROIRegion> > get_checked_regions(void)
    {
        std::vector<std::shared_ptr<ROIRegion> > checked_regions;
        for_each_checked_region([&](std::shared_ptr<ROIRegion> region)
        {
            checked_regions.push_back(region);
        });
        return checked_regions;
    }
    void save_checked_region_label_file(QString filename);
signals:
    void need_update(void);
public:
    std::vector<std::shared_ptr<ROIRegion> > regions;
    int color_gen = 10;
    std::string error_msg;
    bool command(QString cmd,QString param = "",QString param2 = "");
public:
    explicit RegionTableWidget(tracking_window& cur_tracking_window,QWidget *parent = nullptr);

    QColor currentRowColor(void);
    void add_region_from_atlas(std::shared_ptr<atlas> at,unsigned int roi_is);
    void add_all_regions_from_atlas(std::shared_ptr<atlas> at);
    void add_row(int row,QString name,unsigned char type,unsigned int color = 0x00FFFFFF);
    void add_region(QString name,unsigned char type = default_id,unsigned int color = 0x00FFFFFF);
    void begin_update(void);
    void end_update(void);
    void setROIs(ThreadData* data);
    QString getROIname(void);
    template<typename type>
    void add_points(std::vector<tipl::vector<3,type> >& points,bool erase,bool all,float resolution = 1.0)
    {
        if (currentRow() < 0 || currentRow() >= int(regions.size()) ||
            item(currentRow(),0)->checkState() != Qt::Checked)
            return;
        if(all)
        {
            auto regions = get_checked_regions();
            tipl::par_for(regions.size(),[&](int i)
            {
                std::vector<tipl::vector<3,type> > p = points;
                regions[i]->add_points(p,erase,resolution);
            });
        }
        else
            regions[currentRow()]->add_points(points,erase,resolution);
    }

    QString output_format(void);
public slots:
    void updateRegions(QTableWidgetItem* item);

    void draw_region(std::shared_ptr<SliceModel> current_slice,
                     unsigned char dim,int line_width,unsigned char draw_edges,
                     const tipl::color_image& slice_image,float display_ratio,
                     QImage& scaledimage);
    void new_region(void);
    void new_high_resolution_region(void);
    void copy_region(void);
    void save_region(void);
    void save_all_regions(void);
    void save_all_regions_to_4dnifti(void);
    void save_all_regions_to_dir(void);
    void save_region_info(void);
    void load_region(void);
    void load_region_color(void);
    void save_region_color(void);
    void load_mni_region(void);
    void delete_region(void);
    void delete_all_region(void);

    void check_check_status(int,int);
    void whole_brain(void);
    void show_statistics(void);
    void merge_all(void);
    void check_all(void);
    void uncheck_all(void);
    void move_up(void);
    void move_down(void);
    void undo(void);
    void redo(void);
    void move_slice_to_current_region(void);
    // actions
    void action_smoothing(void){do_action("smoothing");}
    void action_erosion(void){do_action("erosion");}
    void action_dilation(void){do_action("dilation");}
    void action_dilation_by_voxel(void){do_action("dilation_by_voxel");}
    void action_opening(void){do_action("opening");}
    void action_closing(void){do_action("closing");}
    void action_defragment(void){do_action("defragment");}
    void action_negate(void){do_action("negate");}
    void action_flipx(void){do_action("flipx");}
    void action_flipy(void){do_action("flipy");}
    void action_flipz(void){do_action("flipz");}
    void action_shiftx(void){do_action("shiftx");}
    void action_shiftnx(void){do_action("shiftnx");}
    void action_shifty(void){do_action("shifty");}
    void action_shiftny(void){do_action("shiftny");}
    void action_shiftz(void){do_action("shiftz");}
    void action_shiftnz(void){do_action("shiftnz");}
    void action_threshold(void){new_region();do_action("threshold");}
    void action_threshold_current(void){do_action("threshold_current");}
    void action_separate(void){do_action("separate");}
    void action_A_B(void){do_action("A-B");}
    void action_B_A(void){do_action("B-A");}
    void action_AB(void){do_action("A*B");}
    void action_B2A(void){do_action("A<<B");}
    void action_sort_name(void){do_action("sort_name");}
    void action_sort_size(void){do_action("sort_size");}
    void action_sort_x(void){do_action("sort_x");}
    void action_sort_y(void){do_action("sort_y");}
    void action_sort_z(void){do_action("sort_z");}
};

#endif // REGIONTABLEWIDGET_H
