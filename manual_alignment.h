#ifndef MANUAL_ALIGNMENT_H
#define MANUAL_ALIGNMENT_H
#include <future>
#include <QDialog>
#include <QTimer>
#include <QGraphicsScene>
#include "TIPL/tipl.hpp"
#include "fib_data.hpp"
namespace Ui {
class manual_alignment;
}

class fib_data;

class manual_alignment : public QDialog
{
    Q_OBJECT
private:
    tipl::image<3> from_original;
    tipl::image<3> from,to,warped_from;
    tipl::affine_transform<float> arg;
    tipl::vector<3> from_vs,to_vs;
    QGraphicsScene scene[3];
    tipl::color_image buffer[3];
    QImage slice_image[3];
    float from_downsample = 1.0f;
    float to_downsample = 1.0f;
    tipl::thread thread;
    tipl::transformation_matrix<float> T,iT;
    void load_param(void);
public:
    std::vector<tipl::image<3> > other_images;
    std::vector<std::string> other_images_name;
    tipl::matrix<4,4> nifti_srow;
    std::vector<tipl::transformation_matrix<float> > other_image_T;
public:
    QTimer* timer;
    explicit manual_alignment(QWidget *parent,
                              tipl::image<3> from_,
                              const tipl::vector<3>& from_vs,
                              tipl::image<3> to_,
                              const tipl::vector<3>& to_vs,
                              tipl::reg::reg_type reg_type,
                              tipl::reg::cost_type cost_function);
    ~manual_alignment();
    void connect_arg_update();
    void disconnect_arg_update();
    void add_image(const std::string& name,tipl::image<3> new_image,const tipl::transformation_matrix<float>& T)
    {
        other_images_name.push_back(name);
        other_images.push_back(std::move(new_image));
        other_image_T.push_back(T);
    }
    void add_images(std::shared_ptr<fib_data> handle);
    void set_arg(const tipl::affine_transform<float>& arg_min,
                 tipl::transformation_matrix<float> iT);
    tipl::transformation_matrix<float> get_iT(void);
private slots:
    void slice_pos_moved();
    void param_changed();
    void on_buttonBox_accepted();

    void on_buttonBox_rejected();

    void on_switch_view_clicked();

    void on_actionSave_Warpped_Image_triggered();

    void on_advance_options_clicked();

    void on_files_clicked();

    void on_actionSave_Transformation_triggered();

    void on_actionLoad_Transformation_triggered();

public slots:
    void on_rerun_clicked();
    void check_reg();
private:
    Ui::manual_alignment *ui;
    void update_image(void);
};

#endif // MANUAL_ALIGNMENT_H
