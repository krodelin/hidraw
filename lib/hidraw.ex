defmodule Hidraw do
  use GenServer

  def start_link(fd) do
    GenServer.start_link(__MODULE__, [fd, self()])
  end

  def enumerate() do
    executable = :code.priv_dir(:hidraw) ++ '/ex_hidraw'

    port =
      Port.open(
        {:spawn_executable, executable},
        [
          {:args, ["enumerate"]},
          {:packet, 2},
          :use_stdio,
          :binary
        ]
      )

    receive do
      {^port, {:data, <<?r, message :: binary>>}} ->
        :erlang.binary_to_term(message)
    after
      5_000 ->
        Port.close(port)
        []
    end
  end

  def descriptor(pid) do
    GenServer.call(pid, :descriptor)
  end

  def output(pid, binary) do
    GenServer.cast(pid, {:output, binary})
  end

  def init([fd, caller]) do
    executable = :code.priv_dir(:hidraw) ++ '/ex_hidraw'

    port =
      Port.open(
        {:spawn_executable, executable},
        [
          {:args, [fd]},
          {:packet, 2},
          :use_stdio,
          :binary,
          :exit_status
        ]
      )

    send(port, {self(), {:command, <<?d>>}})
    descriptor = receive do
      {^port, {:data, <<?d, message :: binary>>}} ->
        :erlang.binary_to_term(message)
    end

    state = %{port: port, name: fd, callback: caller, descriptor: descriptor}

    {:ok, state}
  end

  def handle_call(:descriptor, _from, %{descriptor: descriptor} = state) do
    {:reply, {:ok, descriptor}, state}
  end

  def handle_cast({:output, binary}, %{port: port} = state) do
    message = :erlang.term_to_binary(binary)
    send(port, {self(), {:command, <<?o, message :: binary>>}})
    {:noreply, state}
  end


  def handle_info({_, {:data, <<?i, message :: binary>>}}, state) do
    input_report = :erlang.binary_to_term(message)
    send(state.callback, {:hidraw, state.name, {:input_report, input_report}})
    {:noreply, state}
  end

  def _handle_info({_, {:data, <<?d, message :: binary>>}}, state) do
    descriptor_report = :erlang.binary_to_term(message)
    send(state.callback, {:hidraw, state.name, {:descriptor_report, descriptor_report}})
    {:noreply, state}
  end

  def handle_info({_, {:data, <<?e, message :: binary>>}}, state) do
    {:error, reason} = :erlang.binary_to_term(message)
    send(state.callback, {:hidraw, state.name, {:error, reason}})
    {:stop, reason, state}
  end

  def handle_info({_, {:exit_status, status}}, state) do
    send(state.callback, {:hidraw, state.name, {:exit, status}})
    {:stop, {:exit, status}, state}
  end

end
