#include <ros/ros.h>


#include <nodelet/nodelet.h>
#include <pluginlib/class_list_macros.h>

#include <image_transport/image_transport.h>


#include <boost/thread.hpp>

#include <vigir_crop_decimate/crop_decimate.h>

#include <flor_perception_msgs/DownSampledImageRequest.h>

namespace vigir_image_proc{


  class CropDecimateNodelet : public nodelet::Nodelet
  {

  public:
    virtual void onInit();

    void connectCb();

    virtual void imageCb(const sensor_msgs::ImageConstPtr& image_msg,
    const sensor_msgs::CameraInfoConstPtr& info_msg);

    virtual void imageRequestCb(const flor_perception_msgs::DownSampledImageRequestConstPtr& image_request_msg);

    void publishTimerCb(const ros::TimerEvent& event);

    void publishCroppedImage();

  protected:

    // ROS communication
    boost::shared_ptr<image_transport::ImageTransport> it_in_, it_out_;
    image_transport::CameraSubscriber sub_;

    boost::mutex connect_mutex_;
    image_transport::CameraPublisher pub_;

    ros::Subscriber image_req_sub_;

    int queue_size_;
    double max_video_framerate_;

    CropDecimate crop_decimate_;
    vigir_image_proc::CropDecimate::CropDecimateConfig crop_decimate_config_;
    bool crop_decimate_configured_;

    sensor_msgs::ImageConstPtr last_image_msg_;
    sensor_msgs::CameraInfoConstPtr last_info_msg_;
    flor_perception_msgs::DownSampledImageRequestConstPtr last_request_;

    ros::Timer image_publish_timer_;

  };


  void CropDecimateNodelet::onInit()
  {
    crop_decimate_configured_ = false;

    ros::NodeHandle& nh         = getNodeHandle();
    ros::NodeHandle& private_nh = getPrivateNodeHandle();
    ros::NodeHandle nh_in (nh, "camera");
    ros::NodeHandle nh_out(nh, "camera_out");
    it_in_ .reset(new image_transport::ImageTransport(nh_in));
    it_out_.reset(new image_transport::ImageTransport(nh_out));

    // Read parameters
    private_nh.param("queue_size", queue_size_, 5);
    private_nh.param("max_video_framerate", max_video_framerate_, 100.0);

    // Monitor whether anyone is subscribed to the output
    image_transport::SubscriberStatusCallback connect_cb = boost::bind(&CropDecimateNodelet::connectCb, this);
    ros::SubscriberStatusCallback connect_cb_info = boost::bind(&CropDecimateNodelet::connectCb, this);
    // Make sure we don't enter connectCb() between advertising and assigning to pub_
    boost::lock_guard<boost::mutex> lock(connect_mutex_);
    pub_ = it_out_->advertiseCamera("image_raw",  1, connect_cb, connect_cb, connect_cb_info, connect_cb_info);

    image_req_sub_ = nh_out.subscribe("image_request",1, &CropDecimateNodelet::imageRequestCb, this);

  }

  // Handles (un)subscribing when clients (un)subscribe
  void CropDecimateNodelet::connectCb()
  {
    boost::lock_guard<boost::mutex> lock(connect_mutex_);
    if (pub_.getNumSubscribers() == 0)
      sub_.shutdown();
    else if (!sub_)
    {
      image_transport::TransportHints hints("raw", ros::TransportHints(), getPrivateNodeHandle());
      sub_ = it_in_->subscribeCamera("image_raw", queue_size_, &CropDecimateNodelet::imageCb, this, hints);
      ROS_INFO("subscribed to camera");
    }
  }

  void CropDecimateNodelet::imageCb(const sensor_msgs::ImageConstPtr& image_msg,
  const sensor_msgs::CameraInfoConstPtr& info_msg)
  {
    last_image_msg_ = image_msg;
    last_info_msg_ = info_msg;

    // If we didn't get a request yet, do nothing
    if (!last_request_){
      return;
    }

    //Free run (direct republish) if in ALL mode
    if (last_request_->mode == flor_perception_msgs::DownSampledImageRequest::ALL){
      this->publishCroppedImage();
    }
  }

  void CropDecimateNodelet::imageRequestCb(const flor_perception_msgs::DownSampledImageRequestConstPtr& image_request_msg)
  {

    ROS_INFO("Image requested");

    last_request_ = image_request_msg;

    crop_decimate_config_.decimation_x = image_request_msg->binning_x;
    crop_decimate_config_.decimation_y = image_request_msg->binning_y;
    crop_decimate_config_.width = image_request_msg->roi.width;
    crop_decimate_config_.height = image_request_msg->roi.height;
    crop_decimate_config_.x_offset = image_request_msg->roi.x_offset;
    crop_decimate_config_.y_offset = image_request_msg->roi.y_offset;

    crop_decimate_configured_ = true;

    if (last_request_->mode == flor_perception_msgs::DownSampledImageRequest::ONCE){

      this->publishCroppedImage();

    }else if (last_request_->mode == flor_perception_msgs::DownSampledImageRequest::PUBLISH_FREQ){
      this->publishCroppedImage();

      ros::NodeHandle& nh = getNodeHandle();
      
      double capped_publish_frequency = std::min( max_video_framerate_,  (double)(last_request_->publish_frequency) );

      if(capped_publish_frequency > 0.0f)
        image_publish_timer_ = nh.createTimer(ros::Duration(1.0/capped_publish_frequency), &CropDecimateNodelet::publishTimerCb, this);

    }else{
      //free run/publish always on receive
      this->publishCroppedImage();
    }

  }

  void CropDecimateNodelet::publishTimerCb(const ros::TimerEvent& event)
  {
    //Only actually do something if we're in PUBLISH_FREQ mode.
    if (last_request_->mode != flor_perception_msgs::DownSampledImageRequest::PUBLISH_FREQ){
      return;
    }

    this->publishCroppedImage();
  }

  void CropDecimateNodelet::publishCroppedImage()
  {
    sensor_msgs::ImagePtr image_out;
    sensor_msgs::CameraInfoPtr camera_info_out;

    // Need to make sure we have the last image/info before we try to process it.
    if(last_image_msg_ != NULL && last_info_msg_ != NULL) {
      if (crop_decimate_.processImage(crop_decimate_config_, last_image_msg_, last_info_msg_, image_out, camera_info_out)){
        pub_.publish(image_out, camera_info_out);
      }
    }
  }

}

PLUGINLIB_DECLARE_CLASS (vigir_crop_decimate_nodelet, CropDecimateNodelet, vigir_image_proc::CropDecimateNodelet, nodelet::Nodelet);



